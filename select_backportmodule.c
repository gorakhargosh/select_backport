/* select26 
 *
 * Drop in replacement for select module with the new API 
 * functions from Python 2.6
 */

#include "Python.h"
#include <structmember.h>

#if defined(HAVE_POLL_H)
#include <poll.h>
#elif defined(HAVE_SYS_POLL_H)
#include <sys/poll.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#define SOCKET int
#ifndef PyVarObject_HEAD_INIT
#define PyVarObject_HEAD_INIT(type, size)       \
	PyObject_HEAD_INIT(type) size,
#endif
#ifndef Py_TYPE
#define Py_TYPE(ob) (((PyObject*)(ob))->ob_type)
#endif
#ifndef Py_ssize_t
#define Py_ssize_t ssize_t
#endif
#ifndef Py_RETURN_TRUE
#define Py_RETURN_TRUE return Py_INCREF(Py_True), Py_True
#endif
#ifndef Py_RETURN_FALSE
#define Py_RETURN_FALSE return Py_INCREF(Py_False), Py_False
#endif
#ifndef Py_RETURN_NONE
#define Py_RETURN_NONE return Py_INCREF(Py_None), Py_None
#endif
#ifndef Py_CLEAR
#define Py_CLEAR(op)				\
	do {					\
		if (op) {			\
			PyObject *tmp = (PyObject *)(op);	\
			(op) = NULL;		\
			Py_DECREF(tmp);		\
		}				\
	} while (0)
#endif
#if Py_VERSION_HEX < 0x02040000
PyObject *
PyTuple_Pack(Py_ssize_t n, ...)
{
        Py_ssize_t i;
        PyObject *o;
        PyObject *result;
        PyObject **items;
        va_list vargs;

        va_start(vargs, n);
        result = PyTuple_New(n);
        if (result == NULL)
                return NULL;
        items = ((PyTupleObject *)result)->ob_item;
        for (i = 0; i < n; i++) {
                o = va_arg(vargs, PyObject *);
                Py_INCREF(o);
                items[i] = o;
        }
        va_end(vargs);
        return result;
}
#endif

#ifdef HAVE_EPOLL
/* **************************************************************************
 *                      epoll interface for Linux 2.6
 *
 * Written by Christian Heimes
 * Inspired by Twisted's _epoll.pyx and select.poll()
 */

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#endif

typedef struct {
	PyObject_HEAD
	SOCKET epfd;			/* epoll control file descriptor */
} pyEpoll_Object;

static PyTypeObject pyEpoll_Type;
#define pyepoll_CHECK(op) (PyObject_TypeCheck((op), &pyEpoll_Type))

static PyObject *
pyepoll_err_closed(void)
{
	PyErr_SetString(PyExc_ValueError, "I/O operation on closed epoll fd");
	return NULL;
}

static int
pyepoll_internal_close(pyEpoll_Object *self)
{
	int save_errno = 0;
	if (self->epfd >= 0) {
		int epfd = self->epfd;
		self->epfd = -1;
		Py_BEGIN_ALLOW_THREADS
		if (close(epfd) < 0)
			save_errno = errno;
		Py_END_ALLOW_THREADS
	}
	return save_errno;
}

static PyObject *
newPyEpoll_Object(PyTypeObject *type, int sizehint, SOCKET fd)
{
	pyEpoll_Object *self;
	
	if (sizehint == -1) {
		sizehint = FD_SETSIZE-1;
	}
	else if (sizehint < 1) {
		PyErr_Format(PyExc_ValueError,
			     "sizehint must be greater zero, got %d",
			     sizehint);
		return NULL;
	}

	assert(type != NULL && type->tp_alloc != NULL);
	self = (pyEpoll_Object *) type->tp_alloc(type, 0);
	if (self == NULL)
		return NULL;

	if (fd == -1) {
		Py_BEGIN_ALLOW_THREADS
		self->epfd = epoll_create(sizehint);
		Py_END_ALLOW_THREADS
	}
	else {
		self->epfd = fd;
	}
	if (self->epfd < 0) {
		Py_DECREF(self);
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}
	return (PyObject *)self;
}


static PyObject *
pyepoll_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	int sizehint = -1;
	static char *kwlist[] = {"sizehint", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:epoll", kwlist,
					 &sizehint))
		return NULL;

	return newPyEpoll_Object(type, sizehint, -1);
}


static void
pyepoll_dealloc(pyEpoll_Object *self)
{
	(void)pyepoll_internal_close(self);
	Py_TYPE(self)->tp_free(self);
}

static PyObject*
pyepoll_close(pyEpoll_Object *self)
{
	errno = pyepoll_internal_close(self);
	if (errno < 0) {
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}
	Py_RETURN_NONE;
}

PyDoc_STRVAR(pyepoll_close_doc,
"close() -> None\n\
\n\
Close the epoll control file descriptor. Further operations on the epoll\n\
object will raise an exception.");

static PyObject*
pyepoll_get_closed(pyEpoll_Object *self)
{
	if (self->epfd < 0)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}

static PyObject*
pyepoll_fileno(pyEpoll_Object *self)
{
	if (self->epfd < 0)
		return pyepoll_err_closed();
	return PyInt_FromLong(self->epfd);
}

PyDoc_STRVAR(pyepoll_fileno_doc,
"fileno() -> int\n\
\n\
Return the epoll control file descriptor.");

static PyObject*
pyepoll_fromfd(PyObject *cls, PyObject *args)
{
	SOCKET fd;

	if (!PyArg_ParseTuple(args, "i:fromfd", &fd))
		return NULL;

	return newPyEpoll_Object((PyTypeObject*)cls, -1, fd);
}

PyDoc_STRVAR(pyepoll_fromfd_doc,
"fromfd(fd) -> epoll\n\
\n\
Create an epoll object from a given control fd.");

static PyObject *
pyepoll_internal_ctl(int epfd, int op, PyObject *pfd, unsigned int events)
{
	struct epoll_event ev;
	int result;
	int fd;

	if (epfd < 0)
		return pyepoll_err_closed();

	fd = PyObject_AsFileDescriptor(pfd);
	if (fd == -1) {
		return NULL;
	}

	switch(op) {
	    case EPOLL_CTL_ADD:
	    case EPOLL_CTL_MOD:
		ev.events = events;
		ev.data.fd = fd;
		Py_BEGIN_ALLOW_THREADS
		result = epoll_ctl(epfd, op, fd, &ev);
		Py_END_ALLOW_THREADS
		break;
	    case EPOLL_CTL_DEL:
		/* In kernel versions before 2.6.9, the EPOLL_CTL_DEL
		 * operation required a non-NULL pointer in event, even
		 * though this argument is ignored. */
		Py_BEGIN_ALLOW_THREADS
		result = epoll_ctl(epfd, op, fd, &ev);
		if (errno == EBADF) {
			/* fd already closed */
			result = 0;
			errno = 0;
		}
		Py_END_ALLOW_THREADS
		break;
	    default:
		result = -1;
		errno = EINVAL;
	}

	if (result < 0) {
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *
pyepoll_register(pyEpoll_Object *self, PyObject *args, PyObject *kwds)
{
	PyObject *pfd;
	unsigned int events = EPOLLIN | EPOLLOUT | EPOLLPRI;
	static char *kwlist[] = {"fd", "eventmask", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|I:register", kwlist,
					 &pfd, &events)) {
		return NULL;
	}

	return pyepoll_internal_ctl(self->epfd, EPOLL_CTL_ADD, pfd, events);
}

PyDoc_STRVAR(pyepoll_register_doc,
"register(fd[, eventmask]) -> bool\n\
\n\
Registers a new fd or modifies an already registered fd. register() returns\n\
True if a new fd was registered or False if the event mask for fd was modified.\n\
fd is the target file descriptor of the operation.\n\
events is a bit set composed of the various EPOLL constants; the default\n\
is EPOLL_IN | EPOLL_OUT | EPOLL_PRI.\n\
\n\
The epoll interface supports all file descriptors that support poll.");

static PyObject *
pyepoll_modify(pyEpoll_Object *self, PyObject *args, PyObject *kwds)
{
	PyObject *pfd;
	unsigned int events;
	static char *kwlist[] = {"fd", "eventmask", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "OI:modify", kwlist,
					 &pfd, &events)) {
		return NULL;
	}

	return pyepoll_internal_ctl(self->epfd, EPOLL_CTL_MOD, pfd, events);
}

PyDoc_STRVAR(pyepoll_modify_doc,
"modify(fd, eventmask) -> None\n\
\n\
fd is the target file descriptor of the operation\n\
events is a bit set composed of the various EPOLL constants");

static PyObject *
pyepoll_unregister(pyEpoll_Object *self, PyObject *args, PyObject *kwds)
{
	PyObject *pfd;
	static char *kwlist[] = {"fd", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:unregister", kwlist,
					 &pfd)) {
		return NULL;
	}

	return pyepoll_internal_ctl(self->epfd, EPOLL_CTL_DEL, pfd, 0);
}

PyDoc_STRVAR(pyepoll_unregister_doc,
"unregister(fd) -> None\n\
\n\
fd is the target file descriptor of the operation.");

static PyObject *
pyepoll_poll(pyEpoll_Object *self, PyObject *args, PyObject *kwds)
{
	double dtimeout = -1.;
	int timeout;
	int maxevents = -1;
	int nfds, i;
	PyObject *elist = NULL, *etuple = NULL;
	struct epoll_event *evs = NULL;
	static char *kwlist[] = {"timeout", "maxevents", NULL};

	if (self->epfd < 0)
		return pyepoll_err_closed();

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|di:poll", kwlist,
					 &dtimeout, &maxevents)) {
		return NULL;
	}

	if (dtimeout < 0) {
		timeout = -1;
	}
	else if (dtimeout * 1000.0 > INT_MAX) {
		PyErr_SetString(PyExc_OverflowError,
				"timeout is too large");
		return NULL;
	}
	else {
		timeout = (int)(dtimeout * 1000.0);
	}

	if (maxevents == -1) {
		maxevents = FD_SETSIZE-1;
	}
	else if (maxevents < 1) {
		PyErr_Format(PyExc_ValueError,
			     "maxevents must be greater than 0, got %d",
			     maxevents);
		return NULL;
	}

	evs = PyMem_New(struct epoll_event, maxevents);
	if (evs == NULL) {
		Py_DECREF(self);
		PyErr_NoMemory();
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	nfds = epoll_wait(self->epfd, evs, maxevents, timeout);
	Py_END_ALLOW_THREADS
	if (nfds < 0) {
		PyErr_SetFromErrno(PyExc_IOError);
		goto error;
	}

	elist = PyList_New(nfds);
	if (elist == NULL) {
		goto error;
	}

	for (i = 0; i < nfds; i++) {
		etuple = Py_BuildValue("iI", evs[i].data.fd, evs[i].events);
		if (etuple == NULL) {
			Py_CLEAR(elist);
			goto error;
		}
		PyList_SET_ITEM(elist, i, etuple);
	}

    error:
	PyMem_Free(evs);
	return elist;
}

PyDoc_STRVAR(pyepoll_poll_doc,
"poll([timeout=-1[, maxevents=-1]]) -> [(fd, events), (...)]\n\
\n\
Wait for events on the epoll file descriptor for a maximum time of timeout\n\
in seconds (as float). -1 makes poll wait indefinitely.\n\
Up to maxevents are returned to the caller.");

static PyMethodDef pyepoll_methods[] = {
	{"fromfd",	(PyCFunction)pyepoll_fromfd,
	 METH_VARARGS | METH_CLASS, pyepoll_fromfd_doc},
	{"close",	(PyCFunction)pyepoll_close,	METH_NOARGS,
	 pyepoll_close_doc},
	{"fileno",	(PyCFunction)pyepoll_fileno,	METH_NOARGS,
	 pyepoll_fileno_doc},
	{"modify",	(PyCFunction)pyepoll_modify,
	 METH_VARARGS | METH_KEYWORDS,	pyepoll_modify_doc},
	{"register",	(PyCFunction)pyepoll_register,
	 METH_VARARGS | METH_KEYWORDS,	pyepoll_register_doc},
	{"unregister",	(PyCFunction)pyepoll_unregister,
	 METH_VARARGS | METH_KEYWORDS,	pyepoll_unregister_doc},
	{"poll",	(PyCFunction)pyepoll_poll,
	 METH_VARARGS | METH_KEYWORDS,	pyepoll_poll_doc},
	{NULL,	NULL},
};

static PyGetSetDef pyepoll_getsetlist[] = {
	{"closed", (getter)pyepoll_get_closed, NULL,
	 "True if the epoll handler is closed"},
	{0},
};

PyDoc_STRVAR(pyepoll_doc,
"select26.epoll([sizehint=-1])\n\
\n\
Returns an epolling object\n\
\n\
sizehint must be a positive integer or -1 for the default size. The\n\
sizehint is used to optimize internal data structures. It doesn't limit\n\
the maximum number of monitored events.");

static PyTypeObject pyEpoll_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"select26.epoll",					/* tp_name */
	sizeof(pyEpoll_Object),				/* tp_basicsize */
	0,						/* tp_itemsize */
	(destructor)pyepoll_dealloc,			/* tp_dealloc */
	0,						/* tp_print */
	0,						/* tp_getattr */
	0,						/* tp_setattr */
	0,						/* tp_compare */
	0,						/* tp_repr */
	0,						/* tp_as_number */
	0,						/* tp_as_sequence */
	0,						/* tp_as_mapping */
	0,						/* tp_hash */
	0,              				/* tp_call */
	0,						/* tp_str */
	PyObject_GenericGetAttr,			/* tp_getattro */
	0,						/* tp_setattro */
	0,						/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,				/* tp_flags */
	pyepoll_doc,					/* tp_doc */
	0,						/* tp_traverse */
	0,						/* tp_clear */
	0,						/* tp_richcompare */
	0,						/* tp_weaklistoffset */
	0,						/* tp_iter */
	0,						/* tp_iternext */
	pyepoll_methods,				/* tp_methods */
	0,						/* tp_members */
	pyepoll_getsetlist,				/* tp_getset */
	0,						/* tp_base */
	0,						/* tp_dict */
	0,						/* tp_descr_get */
	0,						/* tp_descr_set */
	0,						/* tp_dictoffset */
	0,						/* tp_init */
	0,						/* tp_alloc */
	pyepoll_new,					/* tp_new */
	0,						/* tp_free */
};

#endif /* HAVE_EPOLL */

#ifdef HAVE_KQUEUE
/* **************************************************************************
 *                      kqueue interface for BSD
 *
 * Copyright (c) 2000 Doug White, 2006 James Knight, 2007 Christian Heimes
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#endif

PyDoc_STRVAR(kqueue_event_doc,
"kevent(ident, filter=KQ_FILTER_READ, flags=KQ_ADD, fflags=0, data=0, udata=0)\n\
\n\
This object is the equivalent of the struct kevent for the C API.\n\
\n\
See the kqueue manpage for more detailed information about the meaning\n\
of the arguments.\n\
\n\
One minor note: while you might hope that udata could store a\n\
reference to a python object, it cannot, because it is impossible to\n\
keep a proper reference count of the object once it's passed into the\n\
kernel. Therefore, I have restricted it to only storing an integer.  I\n\
recommend ignoring it and simply using the 'ident' field to key off\n\
of. You could also set up a dictionary on the python side to store a\n\
udata->object mapping.");

typedef struct {
	PyObject_HEAD
	struct kevent e;
} kqueue_event_Object;

static PyTypeObject kqueue_event_Type;

#define kqueue_event_Check(op) (PyObject_TypeCheck((op), &kqueue_event_Type))

typedef struct {
	PyObject_HEAD
	SOCKET kqfd;		/* kqueue control fd */
} kqueue_queue_Object;

static PyTypeObject kqueue_queue_Type;

#define kqueue_queue_Check(op) (PyObject_TypeCheck((op), &kqueue_queue_Type))

/* Unfortunately, we can't store python objects in udata, because
 * kevents in the kernel can be removed without warning, which would
 * forever lose the refcount on the object stored with it.
 */

#define KQ_OFF(x) offsetof(kqueue_event_Object, x)
static struct PyMemberDef kqueue_event_members[] = {
	{"ident",	T_UINT,		KQ_OFF(e.ident)},
	{"filter",	T_SHORT,	KQ_OFF(e.filter)},
	{"flags",	T_USHORT,	KQ_OFF(e.flags)},
	{"fflags",	T_UINT,		KQ_OFF(e.fflags)},
	{"data",	T_INT,		KQ_OFF(e.data)},
	{"udata",	T_INT,		KQ_OFF(e.udata)},
	{NULL} /* Sentinel */
};
#undef KQ_OFF

static PyObject *
kqueue_event_repr(kqueue_event_Object *s)
{
	char buf[1024];
	PyOS_snprintf(
		buf, sizeof(buf),
		"<select26.kevent ident=%lu filter=%d flags=0x%x fflags=0x%x "
		"data=0x%lx udata=%p>",
		(unsigned long)(s->e.ident), s->e.filter, s->e.flags,
		s->e.fflags, (long)(s->e.data), s->e.udata);
	return PyString_FromString(buf);
}

static int
kqueue_event_init(kqueue_event_Object *self, PyObject *args, PyObject *kwds)
{
	PyObject *pfd;
	static char *kwlist[] = {"ident", "filter", "flags", "fflags",
				 "data", "udata", NULL};

	EV_SET(&(self->e), 0, EVFILT_READ, EV_ADD, 0, 0, 0); /* defaults */
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|hhiii:kevent", kwlist,
		&pfd, &(self->e.filter), &(self->e.flags),
		&(self->e.fflags), &(self->e.data), &(self->e.udata))) {
		return -1;
	}

	self->e.ident = PyObject_AsFileDescriptor(pfd);
	if (self->e.ident == -1) {
		return -1;
	}
	return 0;
}

static PyObject *
kqueue_event_richcompare(kqueue_event_Object *s, kqueue_event_Object *o,
			 int op)
{
	int result = 0;

	if (!kqueue_event_Check(o)) {
		if (op == Py_EQ || op == Py_NE) {
                	PyObject *res = op == Py_EQ ? Py_False : Py_True;
			Py_INCREF(res);
			return res;
		}
		PyErr_Format(PyExc_TypeError,
			"can't compare %.200s to %.200s",
			Py_TYPE(s)->tp_name, Py_TYPE(o)->tp_name);
		return NULL;
	}
	if (((result = s->e.ident - o->e.ident) == 0) &&
	    ((result = s->e.filter - o->e.filter) == 0) &&
	    ((result = s->e.flags - o->e.flags) == 0) &&
	    ((result = s->e.fflags - o->e.fflags) == 0) &&
	    ((result = s->e.data - o->e.data) == 0) &&
	    ((result = s->e.udata - o->e.udata) == 0)
	   ) {
		result = 0;
	}

	switch (op) {
	    case Py_EQ:
		result = (result == 0);
		break;
	    case Py_NE:
		result = (result != 0);
		break;
	    case Py_LE:
		result = (result <= 0);
		break;
	    case Py_GE:
		result = (result >= 0);
		break;
	    case Py_LT:
		result = (result < 0);
		break;
	    case Py_GT:
		result = (result > 0);
		break;
	}
	return PyBool_FromLong(result);
}

static PyTypeObject kqueue_event_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"select26.kevent",				/* tp_name */
	sizeof(kqueue_event_Object),			/* tp_basicsize */
	0,						/* tp_itemsize */
	0,						/* tp_dealloc */
	0,						/* tp_print */
	0,						/* tp_getattr */
	0,						/* tp_setattr */
	0,						/* tp_compare */
	(reprfunc)kqueue_event_repr,			/* tp_repr */
	0,						/* tp_as_number */
	0,						/* tp_as_sequence */
	0,						/* tp_as_mapping */
	0,						/* tp_hash */
	0,              				/* tp_call */
	0,						/* tp_str */
	0,						/* tp_getattro */
	0,						/* tp_setattro */
	0,						/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,				/* tp_flags */
	kqueue_event_doc,				/* tp_doc */
	0,						/* tp_traverse */
	0,						/* tp_clear */
	(richcmpfunc)kqueue_event_richcompare,		/* tp_richcompare */
	0,						/* tp_weaklistoffset */
	0,						/* tp_iter */
	0,						/* tp_iternext */
	0,						/* tp_methods */
	kqueue_event_members,				/* tp_members */
	0,						/* tp_getset */
	0,						/* tp_base */
	0,						/* tp_dict */
	0,						/* tp_descr_get */
	0,						/* tp_descr_set */
	0,						/* tp_dictoffset */
	(initproc)kqueue_event_init,			/* tp_init */
	0,						/* tp_alloc */
	0,						/* tp_new */
	0,						/* tp_free */
};

static PyObject *
kqueue_queue_err_closed(void)
{
	PyErr_SetString(PyExc_ValueError, "I/O operation on closed kqueue fd");
	return NULL;
}

static int
kqueue_queue_internal_close(kqueue_queue_Object *self)
{
	int save_errno = 0;
	if (self->kqfd >= 0) {
		int kqfd = self->kqfd;
		self->kqfd = -1;
		Py_BEGIN_ALLOW_THREADS
		if (close(kqfd) < 0)
			save_errno = errno;
		Py_END_ALLOW_THREADS
	}
	return save_errno;
}

static PyObject *
newKqueue_Object(PyTypeObject *type, SOCKET fd)
{
	kqueue_queue_Object *self;
	assert(type != NULL && type->tp_alloc != NULL);
	self = (kqueue_queue_Object *) type->tp_alloc(type, 0);
	if (self == NULL) {
		return NULL;
	}
	
	if (fd == -1) {
		Py_BEGIN_ALLOW_THREADS
		self->kqfd = kqueue();
		Py_END_ALLOW_THREADS
	}
	else {
		self->kqfd = fd;
	}
	if (self->kqfd < 0) {
		Py_DECREF(self);
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}
	return (PyObject *)self;
}

static PyObject *
kqueue_queue_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{

	if ((args != NULL && PyObject_Size(args)) ||
			(kwds != NULL && PyObject_Size(kwds))) {
		PyErr_SetString(PyExc_ValueError,
				"select26.kqueue doesn't accept arguments");
		return NULL;
	}

	return newKqueue_Object(type, -1);
}

static void
kqueue_queue_dealloc(kqueue_queue_Object *self)
{
	kqueue_queue_internal_close(self);
	Py_TYPE(self)->tp_free(self);
}

static PyObject*
kqueue_queue_close(kqueue_queue_Object *self)
{
	errno = kqueue_queue_internal_close(self);
	if (errno < 0) {
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}
	Py_RETURN_NONE;
}

PyDoc_STRVAR(kqueue_queue_close_doc,
"close() -> None\n\
\n\
Close the kqueue control file descriptor. Further operations on the kqueue\n\
object will raise an exception.");

static PyObject*
kqueue_queue_get_closed(kqueue_queue_Object *self)
{
	if (self->kqfd < 0)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}

static PyObject*
kqueue_queue_fileno(kqueue_queue_Object *self)
{
	if (self->kqfd < 0)
		return kqueue_queue_err_closed();
	return PyInt_FromLong(self->kqfd);
}

PyDoc_STRVAR(kqueue_queue_fileno_doc,
"fileno() -> int\n\
\n\
Return the kqueue control file descriptor.");

static PyObject*
kqueue_queue_fromfd(PyObject *cls, PyObject *args)
{
	SOCKET fd;

	if (!PyArg_ParseTuple(args, "i:fromfd", &fd))
		return NULL;

	return newKqueue_Object((PyTypeObject*)cls, fd);
}

PyDoc_STRVAR(kqueue_queue_fromfd_doc,
"fromfd(fd) -> kqueue\n\
\n\
Create a kqueue object from a given control fd.");

static PyObject *
kqueue_queue_control(kqueue_queue_Object *self, PyObject *args)
{
	int nevents = 0;
	int gotevents = 0;
	int nchanges = 0;
	int i = 0;
	PyObject *otimeout = NULL;
	PyObject *ch = NULL;
	PyObject *it = NULL, *ei = NULL;
	PyObject *result = NULL;
	struct kevent *evl = NULL;
	struct kevent *chl = NULL;
	struct timespec timeoutspec;
	struct timespec *ptimeoutspec;

	if (self->kqfd < 0)
		return kqueue_queue_err_closed();

	if (!PyArg_ParseTuple(args, "Oi|O:control", &ch, &nevents, &otimeout))
		return NULL;

	if (nevents < 0) {
		PyErr_Format(PyExc_ValueError,
			"Length of eventlist must be 0 or positive, got %d",
			nchanges);
		return NULL;
	}

	if (ch != NULL && ch != Py_None) {
		it = PyObject_GetIter(ch);
		if (it == NULL) {
			PyErr_SetString(PyExc_TypeError,
					"changelist is not iterable");
			return NULL;
		}
		nchanges = PyObject_Size(ch);
		if (nchanges < 0) {
			return NULL;
		}
	}

	if (otimeout == Py_None || otimeout == NULL) {
		ptimeoutspec = NULL;
	}
	else if (PyNumber_Check(otimeout)) {
		double timeout;
		long seconds;

		timeout = PyFloat_AsDouble(otimeout);
		if (timeout == -1 && PyErr_Occurred())
			return NULL;
		if (timeout > (double)LONG_MAX) {
			PyErr_SetString(PyExc_OverflowError,
					"timeout period too long");
			return NULL;
		}
		if (timeout < 0) {
			PyErr_SetString(PyExc_ValueError,
					"timeout must be positive or None");
			return NULL;
		}

		seconds = (long)timeout;
		timeout = timeout - (double)seconds;
		timeoutspec.tv_sec = seconds;
		timeoutspec.tv_nsec = (long)(timeout * 1E9);
		ptimeoutspec = &timeoutspec;
	}
	else {
		PyErr_Format(PyExc_TypeError,
			"timeout argument must be an number "
			"or None, got %.200s",
			Py_TYPE(otimeout)->tp_name);
		return NULL;
	}

	if (nchanges) {
		chl = PyMem_New(struct kevent, nchanges);
		if (chl == NULL) {
			PyErr_NoMemory();
			return NULL;
		}
		while ((ei = PyIter_Next(it)) != NULL) {
			if (!kqueue_event_Check(ei)) {
				Py_DECREF(ei);
				PyErr_SetString(PyExc_TypeError,
					"changelist must be an iterable of "
				 	"select26.kevent objects");
				goto error;
			} else {
				chl[i] = ((kqueue_event_Object *)ei)->e;
			}
			Py_DECREF(ei);
		}
	}
	Py_CLEAR(it);

	/* event list */
	if (nevents) {
		evl = PyMem_New(struct kevent, nevents);
		if (evl == NULL) {
			PyErr_NoMemory();
			return NULL;
		}
	}

	Py_BEGIN_ALLOW_THREADS
	gotevents = kevent(self->kqfd, chl, nchanges,
			   evl, nevents, ptimeoutspec);
	Py_END_ALLOW_THREADS

	if (gotevents == -1) {
		PyErr_SetFromErrno(PyExc_OSError);
		goto error;
	}

	result = PyList_New(gotevents);
	if (result == NULL) {
		goto error;
	}

	for (i=0; i < gotevents; i++) {
		kqueue_event_Object *ch;

		ch = PyObject_New(kqueue_event_Object, &kqueue_event_Type);
		if (ch == NULL) {
			goto error;
		}
		ch->e = evl[i];
		PyList_SET_ITEM(result, i, (PyObject *)ch);
	}
	PyMem_Free(chl);
	PyMem_Free(evl);
	return result;

    error:
	PyMem_Free(chl);
	PyMem_Free(evl);
	Py_XDECREF(result);
	Py_XDECREF(it);
	return NULL;
}

PyDoc_STRVAR(kqueue_queue_control_doc,
"control(changelist, max_events=0[, timeout=None]) -> eventlist\n\
\n\
Calls the kernel kevent function.\n\
- changelist must be a list of kevent objects describing the changes\n\
  to be made to the kernel's watch list or None.\n\
- max_events lets you specify the maximum number of events that the\n\
  kernel will return.\n\
- timeout is the maximum time to wait in seconds, or else None,\n\
  to wait forever. timeout accepts floats for smaller timeouts, too.");


static PyMethodDef kqueue_queue_methods[] = {
	{"fromfd",	(PyCFunction)kqueue_queue_fromfd,
	 METH_VARARGS | METH_CLASS, kqueue_queue_fromfd_doc},
	{"close",	(PyCFunction)kqueue_queue_close,	METH_NOARGS,
	 kqueue_queue_close_doc},
	{"fileno",	(PyCFunction)kqueue_queue_fileno,	METH_NOARGS,
	 kqueue_queue_fileno_doc},
	{"control",	(PyCFunction)kqueue_queue_control,
	 METH_VARARGS ,	kqueue_queue_control_doc},
	{NULL,	NULL},
};

static PyGetSetDef kqueue_queue_getsetlist[] = {
	{"closed", (getter)kqueue_queue_get_closed, NULL,
	 "True if the kqueue handler is closed"},
	{0},
};

PyDoc_STRVAR(kqueue_queue_doc,
"Kqueue syscall wrapper.\n\
\n\
For example, to start watching a socket for input:\n\
>>> kq = kqueue()\n\
>>> sock = socket()\n\
>>> sock.connect((host, port))\n\
>>> kq.control([kevent(sock, KQ_FILTER_WRITE, KQ_EV_ADD)], 0)\n\
\n\
To wait one second for it to become writeable:\n\
>>> kq.control(None, 1, 1000)\n\
\n\
To stop listening:\n\
>>> kq.control([kevent(sock, KQ_FILTER_WRITE, KQ_EV_DELETE)], 0)");

static PyTypeObject kqueue_queue_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"select26.kqueue",				/* tp_name */
	sizeof(kqueue_queue_Object),			/* tp_basicsize */
	0,						/* tp_itemsize */
	(destructor)kqueue_queue_dealloc,		/* tp_dealloc */
	0,						/* tp_print */
	0,						/* tp_getattr */
	0,						/* tp_setattr */
	0,						/* tp_compare */
	0,						/* tp_repr */
	0,						/* tp_as_number */
	0,						/* tp_as_sequence */
	0,						/* tp_as_mapping */
	0,						/* tp_hash */
	0,              				/* tp_call */
	0,						/* tp_str */
	0,						/* tp_getattro */
	0,						/* tp_setattro */
	0,						/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,				/* tp_flags */
	kqueue_queue_doc,				/* tp_doc */
	0,						/* tp_traverse */
	0,						/* tp_clear */
	0,						/* tp_richcompare */
	0,						/* tp_weaklistoffset */
	0,						/* tp_iter */
	0,						/* tp_iternext */
	kqueue_queue_methods,				/* tp_methods */
	0,						/* tp_members */
	kqueue_queue_getsetlist,			/* tp_getset */
	0,						/* tp_base */
	0,						/* tp_dict */
	0,						/* tp_descr_get */
	0,						/* tp_descr_set */
	0,						/* tp_dictoffset */
	0,						/* tp_init */
	0,						/* tp_alloc */
	kqueue_queue_new,				/* tp_new */
	0,						/* tp_free */
};

#endif /* HAVE_KQUEUE */
/* ************************************************************************ */

static PyMethodDef select_methods[] = {
	{0,  	0},	/* sentinel */
};

PyDoc_STRVAR(module_doc,
"This module supports asynchronous I/O on multiple file descriptors.");

PyMODINIT_FUNC
initselect26(void)
{
	PyObject *m, *s, *o;
	m = Py_InitModule3("select26", select_methods, module_doc);
	if (m == NULL)
		return;

        s = PyImport_ImportModule("select");
	if (s == NULL)
		return;
	o = PyObject_GetAttrString(s, "error");
	if (o == NULL)
		return;
	PyModule_AddObject(m, "error", o);
	
	o = PyObject_GetAttrString(s, "select");
	if (o == NULL)
		return;
	PyModule_AddObject(m, "select", o);

	o = PyObject_GetAttrString(s, "poll");
	if (o == NULL) {
		PyErr_Clear();
	}
	else {
		PyModule_AddObject(m, "poll", o);

		PyModule_AddIntConstant(m, "POLLIN", POLLIN);
		PyModule_AddIntConstant(m, "POLLPRI", POLLPRI);
		PyModule_AddIntConstant(m, "POLLOUT", POLLOUT);
		PyModule_AddIntConstant(m, "POLLERR", POLLERR);
		PyModule_AddIntConstant(m, "POLLHUP", POLLHUP);
		PyModule_AddIntConstant(m, "POLLNVAL", POLLNVAL);

#ifdef POLLRDNORM
		PyModule_AddIntConstant(m, "POLLRDNORM", POLLRDNORM);
#endif
#ifdef POLLRDBAND
		PyModule_AddIntConstant(m, "POLLRDBAND", POLLRDBAND);
#endif
#ifdef POLLWRNORM
		PyModule_AddIntConstant(m, "POLLWRNORM", POLLWRNORM);
#endif
#ifdef POLLWRBAND
		PyModule_AddIntConstant(m, "POLLWRBAND", POLLWRBAND);
#endif
#ifdef POLLMSG
		PyModule_AddIntConstant(m, "POLLMSG", POLLMSG);
#endif
	}
	Py_DECREF(s);

#ifdef HAVE_EPOLL
	Py_TYPE(&pyEpoll_Type) = &PyType_Type;
	if (PyType_Ready(&pyEpoll_Type) < 0)
		return;

	Py_INCREF(&pyEpoll_Type);
	PyModule_AddObject(m, "epoll", (PyObject *) &pyEpoll_Type);

	PyModule_AddIntConstant(m, "EPOLLIN", EPOLLIN);
	PyModule_AddIntConstant(m, "EPOLLOUT", EPOLLOUT);
	PyModule_AddIntConstant(m, "EPOLLPRI", EPOLLPRI);
	PyModule_AddIntConstant(m, "EPOLLERR", EPOLLERR);
	PyModule_AddIntConstant(m, "EPOLLHUP", EPOLLHUP);
	PyModule_AddIntConstant(m, "EPOLLET", EPOLLET);
#ifdef EPOLLONESHOT
	/* Kernel 2.6.2+ */
	PyModule_AddIntConstant(m, "EPOLLONESHOT", EPOLLONESHOT);
#endif
	/* PyModule_AddIntConstant(m, "EPOLL_RDHUP", EPOLLRDHUP); */
	PyModule_AddIntConstant(m, "EPOLLRDNORM", EPOLLRDNORM);
	PyModule_AddIntConstant(m, "EPOLLRDBAND", EPOLLRDBAND);
	PyModule_AddIntConstant(m, "EPOLLWRNORM", EPOLLWRNORM);
	PyModule_AddIntConstant(m, "EPOLLWRBAND", EPOLLWRBAND);
	PyModule_AddIntConstant(m, "EPOLLMSG", EPOLLMSG);
#endif /* HAVE_EPOLL */

#ifdef HAVE_KQUEUE
	kqueue_event_Type.tp_new = PyType_GenericNew;
	Py_TYPE(&kqueue_event_Type) = &PyType_Type;
	if(PyType_Ready(&kqueue_event_Type) < 0)
		return;

	Py_INCREF(&kqueue_event_Type);
	PyModule_AddObject(m, "kevent", (PyObject *)&kqueue_event_Type);

	Py_TYPE(&kqueue_queue_Type) = &PyType_Type;
	if(PyType_Ready(&kqueue_queue_Type) < 0)
		return;
	Py_INCREF(&kqueue_queue_Type);
	PyModule_AddObject(m, "kqueue", (PyObject *)&kqueue_queue_Type);
	
	/* event filters */
	PyModule_AddIntConstant(m, "KQ_FILTER_READ", EVFILT_READ);
	PyModule_AddIntConstant(m, "KQ_FILTER_WRITE", EVFILT_WRITE);
	PyModule_AddIntConstant(m, "KQ_FILTER_AIO", EVFILT_AIO);
	PyModule_AddIntConstant(m, "KQ_FILTER_VNODE", EVFILT_VNODE);
	PyModule_AddIntConstant(m, "KQ_FILTER_PROC", EVFILT_PROC);
#ifdef EVFILT_NETDEV
	PyModule_AddIntConstant(m, "KQ_FILTER_NETDEV", EVFILT_NETDEV);
#endif
	PyModule_AddIntConstant(m, "KQ_FILTER_SIGNAL", EVFILT_SIGNAL);
	PyModule_AddIntConstant(m, "KQ_FILTER_TIMER", EVFILT_TIMER);

	/* event flags */
	PyModule_AddIntConstant(m, "KQ_EV_ADD", EV_ADD);
	PyModule_AddIntConstant(m, "KQ_EV_DELETE", EV_DELETE);
	PyModule_AddIntConstant(m, "KQ_EV_ENABLE", EV_ENABLE);
	PyModule_AddIntConstant(m, "KQ_EV_DISABLE", EV_DISABLE);
	PyModule_AddIntConstant(m, "KQ_EV_ONESHOT", EV_ONESHOT);
	PyModule_AddIntConstant(m, "KQ_EV_CLEAR", EV_CLEAR);

	PyModule_AddIntConstant(m, "KQ_EV_SYSFLAGS", EV_SYSFLAGS);
	PyModule_AddIntConstant(m, "KQ_EV_FLAG1", EV_FLAG1);

	PyModule_AddIntConstant(m, "KQ_EV_EOF", EV_EOF);
	PyModule_AddIntConstant(m, "KQ_EV_ERROR", EV_ERROR);

	/* READ WRITE filter flag */
	PyModule_AddIntConstant(m, "KQ_NOTE_LOWAT", NOTE_LOWAT);
	
	/* VNODE filter flags  */
	PyModule_AddIntConstant(m, "KQ_NOTE_DELETE", NOTE_DELETE);
	PyModule_AddIntConstant(m, "KQ_NOTE_WRITE", NOTE_WRITE);
	PyModule_AddIntConstant(m, "KQ_NOTE_EXTEND", NOTE_EXTEND);
	PyModule_AddIntConstant(m, "KQ_NOTE_ATTRIB", NOTE_ATTRIB);
	PyModule_AddIntConstant(m, "KQ_NOTE_LINK", NOTE_LINK);
	PyModule_AddIntConstant(m, "KQ_NOTE_RENAME", NOTE_RENAME);
	PyModule_AddIntConstant(m, "KQ_NOTE_REVOKE", NOTE_REVOKE);

	/* PROC filter flags  */
	PyModule_AddIntConstant(m, "KQ_NOTE_EXIT", NOTE_EXIT);
	PyModule_AddIntConstant(m, "KQ_NOTE_FORK", NOTE_FORK);
	PyModule_AddIntConstant(m, "KQ_NOTE_EXEC", NOTE_EXEC);
	PyModule_AddIntConstant(m, "KQ_NOTE_PCTRLMASK", NOTE_PCTRLMASK);
	PyModule_AddIntConstant(m, "KQ_NOTE_PDATAMASK", NOTE_PDATAMASK);

	PyModule_AddIntConstant(m, "KQ_NOTE_TRACK", NOTE_TRACK);
	PyModule_AddIntConstant(m, "KQ_NOTE_CHILD", NOTE_CHILD);
	PyModule_AddIntConstant(m, "KQ_NOTE_TRACKERR", NOTE_TRACKERR);

	/* NETDEV filter flags */
#ifdef EVFILT_NETDEV
	PyModule_AddIntConstant(m, "KQ_NOTE_LINKUP", NOTE_LINKUP);
	PyModule_AddIntConstant(m, "KQ_NOTE_LINKDOWN", NOTE_LINKDOWN);
	PyModule_AddIntConstant(m, "KQ_NOTE_LINKINV", NOTE_LINKINV);
#endif

#endif /* HAVE_KQUEUE */
}
