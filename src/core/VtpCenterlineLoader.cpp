#include "VtpCenterlineLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

// Use Qt's bundled zlib for raw block decompression.
// Qt ships zlib on all platforms; QtZlib/zlib.h is the cross-platform header.
#include <QByteArray>

// ─────────────────────────────────────────────────────────────────────────────
// Mini binary-VTP parser (appended-data format with zlib compression).
// Avoids a VTK dependency in the core library.
//
// VTP appended-data layout (verified against FullVasculature_Centerlines.vtp):
//   Header: UInt32 compressed block size, then zlib-deflated Float64/Int32/Int64 data.
//   Each DataArray has an 'offset' attribute giving its byte offset from the
//   start of the binary payload (immediately after the '_' marker byte).
//
// We read the XML header to get offsets and types, then decompress each block.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ── Minimal XML attribute extractor ────────────────────────────────────────
std::string xmlAttr(const std::string &tag, const std::string &key) {
    auto pos = tag.find(key + "=\"");
    if (pos == std::string::npos) return "";
    pos += key.size() + 2;
    auto end = tag.find('"', pos);
    return (end == std::string::npos) ? "" : tag.substr(pos, end - pos);
}

// ── zlib decompressor ────────────────────────────────────────────────────────
// VTP blocks are raw zlib streams — NO size prefix of any kind.
// We decompress into a buffer sized blocksize (from the VTP header).
bool zlibDecompress(const uint8_t *src, size_t srcLen,
                    std::vector<uint8_t> &dst, size_t expectedSize) {
    // Prepend 4-byte big-endian expected size so qUncompress can handle the raw zlib block
    QByteArray in(static_cast<int>(srcLen) + 4, Qt::Uninitialized);
    const auto sz = static_cast<uint32_t>(expectedSize);
    in[0] = char((sz >> 24) & 0xFF);
    in[1] = char((sz >> 16) & 0xFF);
    in[2] = char((sz >>  8) & 0xFF);
    in[3] = char( sz        & 0xFF);
    std::memcpy(in.data() + 4, src, srcLen);
    const QByteArray out = qUncompress(in);
    if (out.isEmpty()) return false;
    dst.assign(reinterpret_cast<const uint8_t*>(out.constData()),
               reinterpret_cast<const uint8_t*>(out.constData()) + out.size());
    return true;
}

// ── Inline base64 decoder (format="binary") ──────────────────────────────────
// VTK writes: b64(header) immediately followed by b64(block0)b64(block1)...
// base64.decode stops at '=' padding, so we split on '=' boundaries.
std::vector<uint8_t> decodeInlineBase64(const std::string &content) {
    QByteArray all;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eqPos = content.find('=', pos);
        size_t segEnd = (eqPos == std::string::npos) ? content.size() : eqPos;
        while (segEnd < content.size() && content[segEnd] == '=') ++segEnd;
        if (segEnd <= pos) break;
        // Add padding so segment length is multiple of 4
        std::string seg(content.c_str() + pos, segEnd - pos);
        while (seg.size() % 4 != 0) seg += '=';
        all += QByteArray::fromBase64(QByteArray(seg.c_str(), (int)seg.size()));
        pos = segEnd;
    }
    return std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(all.constData()),
        reinterpret_cast<const uint8_t*>(all.constData()) + all.size());
}

std::vector<uint8_t> readInlineBlock(const std::string &content) {
    auto raw = decodeInlineBase64(content);
    if (raw.size() < 12) return {};
    uint32_t nblocks, blocksize, lastSize;
    std::memcpy(&nblocks,   raw.data(),     4);
    std::memcpy(&blocksize, raw.data() + 4, 4);
    std::memcpy(&lastSize,  raw.data() + 8, 4);
    std::vector<uint8_t> out;
    size_t ptr = 12 + nblocks * 4;
    for (uint32_t b = 0; b < nblocks; ++b) {
        uint32_t cs = 0;
        std::memcpy(&cs, raw.data() + 12 + b * 4, 4);
        if (ptr + cs > raw.size()) return {};
        size_t expected = (b == nblocks - 1 && lastSize > 0) ? lastSize : blocksize;
        std::vector<uint8_t> blk;
        if (!zlibDecompress(raw.data() + ptr, cs, blk, expected)) return {};
        out.insert(out.end(), blk.begin(), blk.end());
        ptr += cs;
    }
    return out;
}

// ── Block header decoder ─────────────────────────────────────────────────────
// VTK appended data with zlib: 3 UInt32 header = [nblocks, blocksize, last_partial_size]
// followed by nblocks compressed sizes, then the compressed data.
std::vector<uint8_t> readBlock(const uint8_t *payload, size_t payloadSize, size_t offset) {
    const uint8_t *p = payload + offset;
    if (p + 12 > payload + payloadSize) return {};

    uint32_t nblocks, blocksize, lastSize;
    std::memcpy(&nblocks,   p,      4);
    std::memcpy(&blocksize, p + 4,  4);
    std::memcpy(&lastSize,  p + 8,  4);
    p += 12;

    // Read per-block compressed sizes
    std::vector<uint32_t> compSizes(nblocks);
    for (uint32_t b = 0; b < nblocks; ++b) {
        std::memcpy(&compSizes[b], p, 4);
        p += 4;
    }

    // Decompress: each block decompresses to blocksize bytes, except the
    // last which decompresses to lastSize bytes. VTP blocks are raw zlib —
    // no size prefix; we pass the expected size explicitly.
    std::vector<uint8_t> out;
    for (uint32_t b = 0; b < nblocks; ++b) {
        uint32_t cs = compSizes[b];
        if (p + cs > payload + payloadSize) return {};
        size_t expected = (b == nblocks - 1 && lastSize > 0) ? lastSize : blocksize;
        std::vector<uint8_t> block;
        if (!zlibDecompress(p, cs, block, expected)) return {};
        out.insert(out.end(), block.begin(), block.end());
        p += cs;
    }
    return out;
}

// ── Typed array extractor ────────────────────────────────────────────────────
template<typename T>
std::vector<T> extractArray(const std::vector<uint8_t> &raw, int nComponents = 1) {
    size_t n = raw.size() / sizeof(T);
    std::vector<T> out(n);
    std::memcpy(out.data(), raw.data(), n * sizeof(T));
    return out;
}

} // namespace

namespace cn {

bool VtpCenterlineLoader::load(const std::string &vtpPath,
                               CenterlineGraph &out,
                               Report *report) {
    if (report) *report = Report{};

    // ── 1. Read entire file ─────────────────────────────────────────────────
    std::ifstream f(vtpPath, std::ios::binary);
    if (!f) {
        if (report) report->error = "Cannot open: " + vtpPath;
        return false;
    }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    f.close();

    // ── 2. Parse XML header to find data arrays ─────────────────────────────
    // Find AppendedData section
    const std::string marker = "<AppendedData encoding=\"raw\">";
    // Parse entire file as XML string (inline binary VTPs may have huge DataArray content)
    std::string xml(raw.begin(), raw.end());
    auto adPos = xml.find(marker);
    bool hasAppended = (adPos != std::string::npos);
    // format="binary" files have inline base64 — no AppendedData section needed
    if (!hasAppended && xml.find("format=\"binary\"") == std::string::npos) {
        if (report) report->error = "AppendedData not found";
        return false;
    }
    size_t underscorePos = 0;
    if (hasAppended) {
        underscorePos = adPos + marker.size();
        while (underscorePos < raw.size() && raw[underscorePos] != '_') ++underscorePos;
        if (underscorePos >= raw.size()) {
            if (report) report->error = "AppendedData underscore not found"; return false;
        }
        ++underscorePos;
    }
    const uint8_t *payload  = hasAppended ? raw.data() + underscorePos : nullptr;
    size_t  payloadSize = hasAppended ? raw.size() - underscorePos : 0;

    // Parse DataArray tags to get offsets and names
    struct ArrayMeta {
        std::string name;
        std::string type;
        size_t      offset  = 0;
        int         ncomp   = 1;
        std::string scope;    // "point" or "cell"
        bool        isInline = false;   // format="binary"
        std::string content;            // base64 content for inline arrays
    };
    std::vector<ArrayMeta> arrays;
    std::string curScope;

    size_t pos = 0;
    while (pos < xml.size()) {
        auto lt = xml.find('<', pos);
        if (lt == std::string::npos) break;
        auto gt = xml.find('>', lt);
        if (gt == std::string::npos) break;
        std::string tag = xml.substr(lt, gt - lt + 1);

        // Scope setters: only for section tags that don't contain "DataArray"
        // (a DataArray with Name="Points" would otherwise wrongly set scope)
        // Every topology section owns arrays literally named "connectivity" and
        // "offsets", so the section we are inside is the only thing that tells
        // them apart. Verts/Strips/Polys must be tracked too: without them the
        // scope stayed "lines" for everything after <Lines> and the Strips and
        // Polys arrays were indistinguishable from the real ones.
        if (tag.find("DataArray") == std::string::npos) {
            if      (tag.find("PointData") != std::string::npos) curScope = "point";
            else if (tag.find("CellData")  != std::string::npos) curScope = "cell";
            else if (tag.find("<Points")   != std::string::npos) curScope = "points";
            else if (tag.find("<Verts")    != std::string::npos) curScope = "verts";
            else if (tag.find("<Lines")    != std::string::npos) curScope = "lines";
            else if (tag.find("<Strips")   != std::string::npos) curScope = "strips";
            else if (tag.find("<Polys")    != std::string::npos) curScope = "polys";
        }
        // Inline base64 DataArray (format="binary")
        if (tag.find("DataArray") != std::string::npos &&
            tag.find("format=\"binary\"") != std::string::npos) {
            ArrayMeta am;
            am.name     = xmlAttr(tag, "Name");
            am.type     = xmlAttr(tag, "type");
            am.scope    = curScope;
            am.isInline = true;
            std::string nc = xmlAttr(tag, "NumberOfComponents");
            am.ncomp = nc.empty() ? 1 : std::stoi(nc);
            size_t cStart = gt + 1;
            size_t cEnd   = xml.find("</DataArray>", cStart);
            if (cEnd != std::string::npos) {
                std::string raw_cnt = xml.substr(cStart, cEnd - cStart);
                size_t s = raw_cnt.find_first_not_of(" \t\r\n");
                size_t e = raw_cnt.find_last_not_of(" \t\r\n");
                if (s != std::string::npos) raw_cnt = raw_cnt.substr(s, e - s + 1);
                // Strip child XML elements (e.g. <InformationKey>) after base64 data
                size_t lt = raw_cnt.find('<');
                am.content = (lt != std::string::npos) ? raw_cnt.substr(0, lt) : raw_cnt;
                e = am.content.find_last_not_of(" \t\r\n");
                if (e != std::string::npos) am.content = am.content.substr(0, e + 1);
                pos = cEnd + 12;
            }
            arrays.push_back(am);
        }
        if (tag.find("DataArray") != std::string::npos &&
            tag.find("format=\"appended\"") != std::string::npos) {
            ArrayMeta am;
            am.name  = xmlAttr(tag, "Name");
            am.type  = xmlAttr(tag, "type");
            am.scope = curScope;
            std::string nc = xmlAttr(tag, "NumberOfComponents");
            am.ncomp = nc.empty() ? 1 : std::stoi(nc);
            std::string off = xmlAttr(tag, "offset");
            am.offset = off.empty() ? 0 : (size_t)std::stoull(off);
            arrays.push_back(am);
        }
        pos = gt + 1;
    }

    // Helper to find array by name
    auto findArray = [&](const std::string &name) -> const ArrayMeta* {
        for (auto &a : arrays) if (a.name == name) return &a;
        return nullptr;
    };

    // Unified reader: inline base64 or appended binary
    auto readArray = [&](const ArrayMeta *meta) -> std::vector<uint8_t> {
        if (!meta) return {};
        if (meta->isInline) return readInlineBlock(meta->content);
        return readBlock(payload, payloadSize, meta->offset);
    };

    // ── 3. Read Points (Float64 x3) ─────────────────────────────────────────
    const ArrayMeta *ptsMeta = findArray("Points");
    if (!ptsMeta) { if (report) report->error = "Points array missing"; return false; }
    auto ptsRaw = readArray(ptsMeta);
    if (ptsRaw.empty()) {
        if (report) report->error = "Points decompression failed (offset="
                            + std::to_string(ptsMeta->offset) + " payloadSize="
                            + std::to_string(payloadSize) + ")";
        return false;
    }
    auto ptsData = extractArray<double>(ptsRaw);
    size_t nPoints = ptsData.size() / 3;
    if (nPoints == 0) {
        if (report) report->error = "Points array decompressed but empty"; return false;
    }

    // ── 4. Read radius (MaximumInscribedSphereRadius, Float64) ─────────────
    // Try both known radius array names
    const ArrayMeta *radMeta = findArray("MaximumInscribedSphereRadius");
    if (!radMeta) radMeta = findArray("distance_to_surface");  // Simplified_Vessel_2 uses this
    if (!radMeta) radMeta = findArray("Radius");               // other VMTK variants
    if (!radMeta) { if (report) report->error = "Radius array missing (tried MaximumInscribedSphereRadius, distance_to_surface, Radius)"; return false; }
    auto radRaw = readArray(radMeta);
    if (radRaw.empty()) { if (report) report->error = "Radius decompression failed"; return false; }
    auto radData = extractArray<double>(radRaw);

    // ── 5. Read BranchId (Int32, point data) ────────────────────────────────
    const ArrayMeta *brMeta = nullptr;
    for (auto &a : arrays)
        if (a.name == "BranchId" && a.scope == "point") { brMeta = &a; break; }
    std::vector<int32_t> branchIdPt(nPoints, 0);
    if (brMeta) {
        auto brRaw = readArray(brMeta);
        auto tmp = extractArray<int32_t>(brRaw);
        if (tmp.size() == nPoints) branchIdPt = tmp;
    }

    // ── 6. Read Lines connectivity (Int64) and offsets (Int64) ──────────────
    // VTP has multiple connectivity/offsets arrays (Verts=empty, Lines=data, etc).
    // Find Lines by reading nblocks from payload: Verts has nblocks=0, Lines has nblocks>0.
    //
    // Two encodings have to be handled and the old offset-based rule worked for
    // neither reliably:
    //   * appended raw  — every array has a real byte offset; the previous code
    //     peeked at the block header and kept the array with the LARGEST offset.
    //   * inline base64 — there are no offsets at all. `a.offset` is always 0
    //     and `payload` is nullptr, so `a.offset + 4 > payloadSize` (0+4 > 0)
    //     was true for every candidate and all of them were skipped. connMeta
    //     and offMeta stayed null and the load aborted with
    //     "Lines offsets/connectivity not found".
    //
    // The section scope recorded while walking the XML identifies the arrays
    // unambiguously in both encodings, so use that as the primary rule and fall
    // back to "first pair that actually decodes to something" only if a writer
    // emitted a layout we did not anticipate.
    const ArrayMeta *connMeta = nullptr, *offMeta = nullptr;

    for (const auto &a : arrays) {
        if (a.scope != "lines") continue;
        if      (a.name == "connectivity") connMeta = &a;
        else if (a.name == "offsets")      offMeta  = &a;
    }

    if (!connMeta || !offMeta) {
        // Fallback: pick the first connectivity/offsets pair whose block is
        // non-empty. Verts/Strips/Polys are empty in a centerline VTP.
        for (const auto &a : arrays) {
            if (a.name == "connectivity" && !connMeta && !readArray(&a).empty()) connMeta = &a;
            if (a.name == "offsets"      && !offMeta  && !readArray(&a).empty()) offMeta  = &a;
        }
    }
    std::vector<int64_t> connectivity, offsets;
    if (connMeta) {
        auto r = readArray(connMeta);
        connectivity = extractArray<int64_t>(r);
    }
    if (offMeta) {
        auto r = readArray(offMeta);
        offsets = extractArray<int64_t>(r);
    }

    // ── 7. Read ParentBranchId (Int32, cell data) for inter-branch links ────
    const ArrayMeta *parentMeta = nullptr;
    for (auto &a : arrays)
        if (a.name == "ParentBranchId") { parentMeta = &a; break; }
    std::vector<int32_t> parentBranchId;
    if (parentMeta) {
        auto r = readArray(parentMeta);
        parentBranchId = extractArray<int32_t>(r);
    }

    // ── 8. Read BranchId (Int32, cell data) for branch→cell mapping ─────────
    // BranchId appears both in point data and cell data — pick the one with smaller size
    // Cell BranchId has 1333 entries (one per branch), point BranchId has 192192 entries.
    // Prefer an explicit cell scope; fall back to the larger offset for appended files.
    const ArrayMeta *brCellMeta = nullptr;
    for (auto &a : arrays)
        if (a.name == "BranchId" && a.scope == "cell") { brCellMeta = &a; break; }
    if (!brCellMeta)
        for (auto &a : arrays)
            if (a.name == "BranchId" && (!brCellMeta || a.offset > brCellMeta->offset))
                brCellMeta = &a;
    std::vector<int32_t> cellBranchId;
    if (brCellMeta) {
        auto r = readArray(brCellMeta);
        cellBranchId = extractArray<int32_t>(r);
    }

    // ── 8b. Read StartNodeId / EndNodeId (Int32, cell data) ─────────────────
    // These are the authored junction topology: one integer id per line
    // endpoint, shared between every line that meets there. Two lines carrying
    // the same StartNodeId physically start at the same bifurcation. That is
    // exact information, and it makes both the spatial merge and the
    // gap-stitching heuristics unnecessary when it is present.
    const ArrayMeta *startMeta = nullptr, *endMeta = nullptr;
    for (auto &a : arrays) {
        if (a.name == "StartNodeId") startMeta = &a;
        if (a.name == "EndNodeId")   endMeta   = &a;
    }
    std::vector<int32_t> startNodeId, endNodeId;
    if (startMeta) startNodeId = extractArray<int32_t>(readArray(startMeta));
    if (endMeta)   endNodeId   = extractArray<int32_t>(readArray(endMeta));

    // ── 9. Build graph ───────────────────────────────────────────────────────
    if (offsets.empty()) {
        if (report) report->error = "Lines offsets/connectivity not found — "
                            + std::to_string(arrays.size()) + " arrays parsed";
        return false;
    }
    out.clear();

    // ── Graph build ──────────────────────────────────────────────────────────
    //
    // Preferred path: StartNodeId/EndNodeId weld. Every line's first point *is*
    // junction StartNodeId[li] and its last point *is* junction EndNodeId[li],
    // so sharing one graph node per distinct junction id reproduces the authored
    // topology exactly. Interior points stay unique, which keeps the full
    // sampling resolution the catheter rides on.
    //
    // This replaces the old 1 mm spatial merge plus four-pass gap stitching.
    // Those were guesses at connectivity that the file already states outright,
    // and they had two costs: the merge collapsed genuinely distinct points that
    // happened to fall in one grid cell (27,907 points -> 15,968 nodes, degree
    // running up to 7), and the stitching then bridged whatever the merge had
    // torn apart, welding the whole tree into a single blob in which every
    // terminal reported the same reachable count. Access-site selection depends
    // on that count as its primary key, so it degenerated to the tie-break.
    //
    // Fallback path: if the file has no StartNodeId/EndNodeId, drop back to the
    // spatial merge so other VMTK exports still load.
    const bool haveJunctionIds =
        !startNodeId.empty() && !endNodeId.empty() &&
        startNodeId.size() == offsets.size() && endNodeId.size() == offsets.size();

    struct GridKey { int32_t ix,iy,iz;
        bool operator==(const GridKey &o) const { return ix==o.ix&&iy==o.iy&&iz==o.iz; }
    };
    struct GridHash { size_t operator()(const GridKey &k) const {
            return size_t(k.ix)*73856093u ^ size_t(k.iy)*19349663u ^ size_t(k.iz)*83492791u;
        }};
    const float MD = 0.001f;  // 1mm merge distance (fallback path only)
    std::unordered_map<GridKey,int32_t,GridHash> gridMap;

    // Emit a graph node for VTP point `ptIdx`, scaled and radius-clamped.
    auto addPoint = [&](int64_t ptIdx) -> int32_t {
        const float x = float(ptsData[ptIdx*3+0] * m_params.vtpToModelScale);
        const float y = float(ptsData[ptIdx*3+1] * m_params.vtpToModelScale);
        const float z = float(ptsData[ptIdx*3+2] * m_params.vtpToModelScale);
        float r = float((radData.size() > size_t(ptIdx) ? radData[ptIdx] : 0.0)
                        * m_params.vtpToModelScale);
        if (r < m_params.minRadiusM) r = m_params.minRadiusM;
        return out.addNode(Vec3(x,y,z), r, 0);
    };
    auto getNodeMerged = [&](int64_t ptIdx) -> int32_t {
        const float x = float(ptsData[ptIdx*3+0] * m_params.vtpToModelScale);
        const float y = float(ptsData[ptIdx*3+1] * m_params.vtpToModelScale);
        const float z = float(ptsData[ptIdx*3+2] * m_params.vtpToModelScale);
        GridKey k{int32_t(std::floor(x/MD)), int32_t(std::floor(y/MD)), int32_t(std::floor(z/MD))};
        auto it = gridMap.find(k);
        if (it != gridMap.end()) return it->second;
        const int32_t nid = addPoint(ptIdx);
        gridMap[k] = nid;
        return nid;
    };

    const size_t nLines = offsets.size();
    const int maxBranches = m_params.maxBranches > 0 ? m_params.maxBranches : (int)nLines;
    int branchesLoaded = 0;
    std::set<std::pair<int32_t,int32_t>> edgeSet;
    std::unordered_map<int32_t,int32_t> junctionNode;   // VTP node id -> graph node
    int connectorEdges = 0;

    size_t connBase = 0;
    for (size_t li = 0; li < nLines && branchesLoaded < maxBranches; ++li) {
        const int64_t lineEnd = offsets[li];
        std::vector<int64_t> linePts;
        for (int64_t k = int64_t(connBase); k < lineEnd; ++k) {
            const int64_t p = connectivity[size_t(k)];
            if (p >= 0 && size_t(p) < nPoints) linePts.push_back(p);
        }
        connBase = size_t(lineEnd);
        if (linePts.size() < 2) continue;

        int32_t prev = -1;
        for (size_t pi = 0; pi < linePts.size(); ++pi) {
            const bool isFirst = (pi == 0);
            const bool isLast  = (pi + 1 == linePts.size());
            int32_t nid;

            if (haveJunctionIds && (isFirst || isLast)) {
                const int32_t j = isFirst ? startNodeId[li] : endNodeId[li];
                auto it = junctionNode.find(j);
                if (it != junctionNode.end()) {
                    nid = it->second;
                } else {
                    nid = addPoint(linePts[pi]);
                    junctionNode.emplace(j, nid);
                }
            } else if (haveJunctionIds) {
                nid = addPoint(linePts[pi]);
            } else {
                nid = getNodeMerged(linePts[pi]);
            }

            if (prev >= 0 && prev != nid) {
                const auto e = std::make_pair(std::min(prev,nid), std::max(prev,nid));
                if (edgeSet.insert(e).second) out.addLink(prev, nid);
            }
            prev = nid;
        }
        ++branchesLoaded;
    }

    // Fallback path only: the spatial merge fragments the tree, so the old
    // gap-stitching passes are still needed to put it back together.
    int bifLinks = 0;
    if (!haveJunctionIds) {
        const float gaps[] = {0.001f, 0.010f, 0.030f, 0.055f};
        for (float gap : gaps) {
            out.buildAdjacency();
            const auto &nodes = out.nodes();
            const auto  terms = out.terminals();
            for (size_t i = 0; i < terms.size(); ++i) {
                const Vec3 &pi = nodes[terms[i]].position;
                for (size_t j = i + 1; j < terms.size(); ++j) {
                    const Vec3 &pj = nodes[terms[j]].position;
                    if (std::fabs(pi.z - pj.z) > gap) continue;
                    if (std::fabs(pi.x - pj.x) > gap) continue;
                    const float dx = pi.x - pj.x, dy = pi.y - pj.y, dz = pi.z - pj.z;
                    if (std::sqrt(dx*dx + dy*dy + dz*dz) <= gap) {
                        out.addLink(terms[i], terms[j]);
                        ++bifLinks;
                    }
                }
            }
        }
    }
    (void)connectorEdges;
    (void)cellBranchId;
    (void)parentBranchId;
    (void)branchIdPt;

    // ── 12. Build adjacency and optionally smooth ────────────────────────────
    out.buildAdjacency();
    if (m_params.smoothIterations > 0)
        out.smooth(m_params.smoothIterations, m_params.smoothLambda);

    // ── 13. Compute radius statistics ────────────────────────────────────────
    float rMin = 1e9f, rMax = 0.f, rSum = 0.f;
    const float mmPerM = 1000.f;
    for (const auto &n : out.nodes()) {
        float r = n.radius * mmPerM;
        rMin = std::min(rMin, r);
        rMax = std::max(rMax, r);
        rSum += r;
    }
    int nn = static_cast<int>(out.nodeCount());

    // ── 14. Leaf nodes ───────────────────────────────────────────────────────
    // With the junction-id build the graph degree is meaningful, so the true
    // terminals are simply the degree-1 nodes. The old code collected every
    // line's first and last point, which after merging included interior
    // junctions and produced 431 "leaves" for 199 real ones — access-site
    // detection then had hundreds of mid-vessel candidates to choose from.
    std::vector<int32_t> leafNodes = out.terminals();

    if (report) {
        report->branchesLoaded   = branchesLoaded;
        report->pointsLoaded     = nn;
        report->linksCreated     = static_cast<int>(out.links().size()) - bifLinks;
        report->bifurcationLinks = bifLinks;
        report->terminals        = static_cast<int>(leafNodes.size());
        report->bifurcations     = static_cast<int>(out.bifurcations().size());
        report->minRadiusMm      = nn > 0 ? rMin : 0.f;
        report->maxRadiusMm      = nn > 0 ? rMax : 0.f;
        report->meanRadiusMm     = nn > 0 ? rSum / nn : 0.f;
        report->leafNodeIds      = leafNodes;
    }

    return nn > 0;
}

} // namespace cn