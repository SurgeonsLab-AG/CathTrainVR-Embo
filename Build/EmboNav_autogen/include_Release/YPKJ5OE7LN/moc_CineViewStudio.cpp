/****************************************************************************
** Meta object code from reading C++ file 'CineViewStudio.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/ui/CineViewStudio.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CineViewStudio.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14CineViewStudioE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN14CineViewStudioE = QtMocHelpers::stringData(
    "CineViewStudio",
    "backToSimulatorRequested",
    "",
    "onPlayPauseClicked",
    "onResetClicked",
    "onFirstClicked",
    "onLastClicked",
    "onStepForwardClicked",
    "onStepBackwardClicked",
    "onSliderMoved",
    "value",
    "onSpeedButtonClicked",
    "speed",
    "onLoopToggled",
    "onRunSelected",
    "runIndex",
    "onPlaybackTick",
    "onNewAcquisitionClicked"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN14CineViewStudioE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   92,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       3,    0,   93,    2, 0x08,    2 /* Private */,
       4,    0,   94,    2, 0x08,    3 /* Private */,
       5,    0,   95,    2, 0x08,    4 /* Private */,
       6,    0,   96,    2, 0x08,    5 /* Private */,
       7,    0,   97,    2, 0x08,    6 /* Private */,
       8,    0,   98,    2, 0x08,    7 /* Private */,
       9,    1,   99,    2, 0x08,    8 /* Private */,
      11,    1,  102,    2, 0x08,   10 /* Private */,
      13,    0,  105,    2, 0x08,   12 /* Private */,
      14,    1,  106,    2, 0x08,   13 /* Private */,
      16,    0,  109,    2, 0x08,   15 /* Private */,
      17,    0,  110,    2, 0x08,   16 /* Private */,

 // signals: parameters
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   10,
    QMetaType::Void, QMetaType::Double,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   15,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject CineViewStudio::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN14CineViewStudioE.offsetsAndSizes,
    qt_meta_data_ZN14CineViewStudioE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN14CineViewStudioE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CineViewStudio, std::true_type>,
        // method 'backToSimulatorRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPlayPauseClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onResetClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFirstClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onLastClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStepForwardClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStepBackwardClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSliderMoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSpeedButtonClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'onLoopToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRunSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onPlaybackTick'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onNewAcquisitionClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void CineViewStudio::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CineViewStudio *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->backToSimulatorRequested(); break;
        case 1: _t->onPlayPauseClicked(); break;
        case 2: _t->onResetClicked(); break;
        case 3: _t->onFirstClicked(); break;
        case 4: _t->onLastClicked(); break;
        case 5: _t->onStepForwardClicked(); break;
        case 6: _t->onStepBackwardClicked(); break;
        case 7: _t->onSliderMoved((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->onSpeedButtonClicked((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 9: _t->onLoopToggled(); break;
        case 10: _t->onRunSelected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->onPlaybackTick(); break;
        case 12: _t->onNewAcquisitionClicked(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (CineViewStudio::*)();
            if (_q_method_type _q_method = &CineViewStudio::backToSimulatorRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *CineViewStudio::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CineViewStudio::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN14CineViewStudioE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int CineViewStudio::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void CineViewStudio::backToSimulatorRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
