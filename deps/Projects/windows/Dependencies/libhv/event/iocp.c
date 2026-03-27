#include "iowatcher.h"

#ifdef EVENT_IOCP
#include "hplatform.h"
#include "hdef.h"

#include "hevent.h"
#include "overlapio.h"

typedef struct iocp_ctx_s {
    HANDLE      iocp;
} iocp_ctx_t;

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher)    return 0;
    iocp_ctx_t* iocp_ctx;
    HV_ALLOC_SIZEOF(iocp_ctx);
    iocp_ctx->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    loop->iowatcher = iocp_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    CloseHandle(iocp_ctx->iocp);
    HV_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    hio_t* io = loop->ios.ptr[fd];
    if (io && io->events == 0 && events != 0) {
        CreateIoCompletionPort((HANDLE)fd, iocp_ctx->iocp, 0, 0);
    }
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    hio_t* io = loop->ios.ptr[fd];
    if ((io->events & ~events) == 0) {
        CancelIo((HANDLE)fd);
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    if (loop->iowatcher == NULL) return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    int nevents = 0;
    int poll_timeout = timeout;
    // Drain all queued IOCP completions:
    // first call uses caller's timeout, subsequent calls use 0 (non-blocking)
    while (1) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED povlp = NULL;
        BOOL bRet = GetQueuedCompletionStatus(iocp_ctx->iocp, &bytes, &key, &povlp, poll_timeout);
        int err = 0;
        if (povlp == NULL) {
            err = WSAGetLastError();
            if (err == WAIT_TIMEOUT || err == ERROR_NETNAME_DELETED || err == ERROR_OPERATION_ABORTED) {
                break;
            }
            break;
        }
        hoverlapped_t* hovlp = (hoverlapped_t*)povlp;
        hio_t* io = hovlp->io;
        if (io->closed) {
            if (hovlp->event == HV_READ) {
                if (io->io_type == HIO_TYPE_UDP || io->io_type == HIO_TYPE_IP) {
                    HV_FREE(hovlp->buf.buf);
                }
            } else {
                HV_FREE(hovlp->buf.buf);
            }
            HV_FREE(hovlp->addr);
            HV_FREE(hovlp);
            ++nevents;
            poll_timeout = 0;
            continue;
        }
        if (bRet == FALSE) {
            err = WSAGetLastError();
            printd("iocp ret=%d err=%d bytes=%u\n", bRet, err, bytes);
            hovlp->error = err;
        }
        hovlp->bytes = bytes;
        io->hovlp = hovlp;
        io->revents |= hovlp->event;
        // Process this completion IMMEDIATELY (inline) rather than deferring
        // via EVENT_PENDING. This is critical because io->hovlp is a single
        // pointer — if two completions for the same IO arrive in one poll
        // cycle (e.g. READ + WRITE), the second would overwrite the first,
        // causing data loss. Processing inline ensures each hovlp is fully
        // consumed before the next GQCS call.
        if (io->active && io->cb) {
            io->cb(io);  // hio_handle_events: processes & clears revents
        } else {
            io->revents = 0;
        }
        ++nevents;
        poll_timeout = 0;  // subsequent polls are non-blocking
    }
    return nevents;
}
#endif
