#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
<<<<<<< HEAD
#include <QFontDatabase>
=======
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
#include <QPalette>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QTimer>
<<<<<<< HEAD
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSplashScreen>
#include <QScreen>
#include <QGuiApplication>
=======
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7


namespace {

void applyClinicalStyle(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    // Black console theme.
    //
    // This used to install a light beige palette (212,208,200), which is why the
    // menu bar, dialogs and any widget not covered by MainWindow's stylesheet
    // came out pale against an otherwise black application. The palette has to
    // be dark as well as the stylesheet, because Qt draws frames, menus and
    // native-ish chrome from the palette rather than from QSS.
    const QColor black(0, 0, 0);
    const QColor surface(11, 11, 14);      // buttons, raised chrome
    const QColor inset(0, 0, 0);           // text entry backgrounds
    const QColor text(232, 232, 232);
    const QColor dim(154, 154, 162);
    const QColor line(48, 48, 56);
    const QColor highlight(46, 46, 56);

    QPalette p;
    p.setColor(QPalette::Window,          black);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            inset);
    p.setColor(QPalette::AlternateBase,   surface);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          surface);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::white);
    p.setColor(QPalette::Light,           line);
    p.setColor(QPalette::Midlight,        QColor(32, 32, 32));
    p.setColor(QPalette::Mid,             line);
    p.setColor(QPalette::Dark,            QColor(20, 20, 20));
    p.setColor(QPalette::Shadow,          black);
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase,     QColor(20, 20, 20));
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Link,            QColor(171, 171, 171));
    p.setColor(QPalette::Disabled, QPalette::WindowText, dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dim);
    p.setColor(QPalette::Disabled, QPalette::Text,       dim);
    app.setPalette(p);
    app.setFont(QFont("Sans Serif", 9));

    // Application-level sheet for chrome MainWindow's own sheet does not reach:
    // the menu bar, dialogs, tooltips and scrollbars.
    app.setStyleSheet(R"(
        QWidget         { background:#000000; color:#E8E8E8; }
        QDialog         { background:#000000; }
        QMenuBar        { background:#000000; color:#E8E8E8; border-bottom:1px solid #313131; }
        QMenuBar::item:selected { background:#2F2F2F; }
        QMenu           { background:#0B0B0B; color:#E8E8E8; border:1px solid #313131; }
        QMenu::item:selected    { background:#2F2F2F; }
        QToolTip        { background:#141414; color:#E8E8E8; border:1px solid #313131; }
        QGroupBox       { border:1px solid #313131; margin-top:8px; padding-top:6px; }
        QGroupBox::title{ subcontrol-origin:margin; left:7px; padding:0 3px; color:#9B9B9B; }
        QStatusBar      { background:#000000; border-top:1px solid #313131; }
        QPlainTextEdit,
        QTextEdit,
        QLineEdit       { background:#000000; border:1px solid #313131; color:#B1B1B1; }
        QDockWidget::title { background:#000000; padding:4px;
                             border-bottom:1px solid #313131; color:#C9C9C9; }
        QScrollBar:vertical   { background:#000000; width:8px; }
        QScrollBar:horizontal { background:#000000; height:8px; }
        QScrollBar::handle    { background:#313131; border-radius:4px; }
        QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
    )");
}

} // namespace

int main(int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
<<<<<<< HEAD
    QApplication::setApplicationName("CathTrainVR");
    QApplication::setApplicationVersion("1.0");
    applyClinicalStyle(app);

    // Titillium Web, everywhere — the one font this whole UI uses now,
    // per explicit request. It's a Google Font, not something Windows
    // ships by default, so this tries loading actual font files bundled
    // under Data first (several common weight filenames, best-effort - a
    // missing file here is silently skipped, not an error). Regardless of
    // whether a bundled file is found, the family name is still set as the
    // application default: if it turns out to already be installed system-
    // wide, this picks it up directly; if neither a bundled file nor a
    // system install exists, Qt falls back to a substitute font rather
    // than failing, so this is safe either way.
    for (const QString &weight : {"TitilliumWeb-Regular.ttf", "TitilliumWeb-Bold.ttf",
                                  "TitilliumWeb-SemiBold.ttf", "TitilliumWeb-Light.ttf",
                                  "TitilliumWeb-Italic.ttf"}) {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/SurgeonsLab/EmboNav/Data/") + weight);
    }
    QFont appFont(QStringLiteral("Titillium Web"));
    appFont.setStyleHint(QFont::SansSerif);   // Titillium Web is a sans-serif; a sane fallback family if it's unavailable
    QApplication::setFont(appFont);

    // Application/window icon — logomark.png EXCLUSIVELY, per explicit
    // request (splash_logo.png must never be used here). Applies app-wide
    // (every QWidget/QDialog that doesn't set its own icon inherits this),
    // which is what actually makes it the one the OS shows in the taskbar,
    // Alt-Tab switcher, and window title bar, not just MainWindow's own
    // icon property.
    const QIcon appIcon(QStringLiteral("C:/SurgeonsLab/EmboNav/Data/logomark.png"));
    if (!appIcon.isNull())
        QApplication::setWindowIcon(appIcon);

    // Splash screen — splash_logo.png EXCLUSIVELY, per explicit request
    // (logomark.png must never be used here). Shown immediately, before
    // MainWindow's own construction even starts (STL/centerline work
    // included), so the operator sees a full-screen loading screen rather
    // than a blank/frozen window during startup, matching the eventual
    // maximized main window's own footprint rather than a small centred
    // pixmap-sized widget.
    //
    // Deliberately a single exact filename, not a candidate list - an
    // earlier version of this tried several likely names (SL_logo.png,
    // case variants) since the real filename was unclear at the time; now
    // that the two files' exact names and exclusive purposes are both
    // explicitly given, guessing between candidates only risks silently
    // picking up the wrong one for the wrong purpose again.
    QSplashScreen *splash = nullptr;
    QPixmap logo(QStringLiteral("C:/SurgeonsLab/EmboNav/Data/splash_logo.png"));
    if (!logo.isNull()) {
        // Full screen geometry (not availableGeometry — this is a splash
        // screen shown before the taskbar/desktop is relevant to the
        // operator, so it should cover the whole physical screen the same
        // way the maximized main window will fill the whole WORKING area a
        // moment later).
        const QScreen *screen = QGuiApplication::primaryScreen();
        const QSize screenSize = screen ? screen->size() : QSize(1920, 1080);

        // Compose a full-screen-sized canvas with the logo centred and
        // correctly proportioned within it, rather than handing
        // QSplashScreen the raw logo pixmap directly - QSplashScreen sizes
        // its own widget to exactly match whatever pixmap it's given, so a
        // logo-sized pixmap gives a logo-sized (small, centred-by-Qt)
        // splash widget, not a full-screen one. Building the full-screen
        // canvas ourselves is what actually makes the splash SCREEN
        // fullscreen, while the logo drawn onto it keeps its own aspect
        // ratio (scaled to at most 40% of the screen's width/height) rather
        // than being stretched to fill the whole canvas and distorting.
        QPixmap canvas(screenSize);
        canvas.fill(QColor(0, 0, 0));
        const QSize maxLogoSize(screenSize.width() * 0.4, screenSize.height() * 0.4);
        const QPixmap scaledLogo = logo.scaled(maxLogoSize, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
        QPainter painter(&canvas);
        painter.drawPixmap((screenSize.width()  - scaledLogo.width())  / 2,
                           (screenSize.height() - scaledLogo.height()) / 2,
                           scaledLogo);
        painter.end();

        splash = new QSplashScreen(canvas);
        splash->show();
        app.processEvents();   // paint the splash immediately, before any blocking work below
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "CathTrainVR — endovascular embolectomy navigation console.\n"
=======
    QApplication::setApplicationName("EmboNav");
    QApplication::setApplicationVersion("1.0");
    applyClinicalStyle(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "EmboNav — endovascular embolectomy navigation console.\n"
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
        "Open a vessel STL via File > Open to begin.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("mesh", "Vessel surface STL to open on start-up.");
    parser.process(app);

    MainWindow w;
<<<<<<< HEAD
    w.setWindowIcon(appIcon);   // belt-and-suspenders: explicit on the main window too
    w.showMaximized();

    // Splash hands off to the now-visible, maximized main window rather than
    // just closing blind - finish() also raises/activates w for us.
    if (splash) {
        splash->finish(&w);
        delete splash;
    }
=======
    w.show();
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7

    // Defer loading until after the event loop starts so the window
    // is fully visible before any wait cursor or heavy work begins.
    const QStringList args = parser.positionalArguments();
    QTimer::singleShot(0, &w, [&w, args]{
        if (!args.isEmpty())
            w.loadMesh(args.first());
        else
            w.tryAutoLoad();
    });

    return app.exec();
}