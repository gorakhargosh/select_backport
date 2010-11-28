/**
 * select_backport.h: Backporting selectmodule to Python2.5 and Python2.6.
 */
#ifndef _SELECT_BACKPORT_MODULE__H__
#define _SELECT_BACKPORT_MODULE__H__ 1

/**
 * Python 2.5 doesn't have this macro defined.
 */
#ifndef Py_TYPE
#  define Py_TYPE(o) ((o)->ob_type)
#endif /* !Py_TYPE */

#ifndef PyVarObject_HEAD_INIT
#define PyVarObject_HEAD_INIT(type, size) \
        PyObject_HEAD_INIT(type) size,
#endif /* !PyVarObject_HEAD_INIT */

#endif

