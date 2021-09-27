
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


int              ngx_argc;
char           **ngx_argv;
char           **ngx_os_argv;

ngx_int_t        ngx_last_process;
ngx_process_t    ngx_processes[NGX_MAX_PROCESSES];

///****************************************************************************
/// @author  : mxn                                                          
/// @file    :             
/// @brief   : several worker from skylar_srvcomp_win                                                                          
///****************************************************************************

static void ngx_set_worker_id(ngx_cycle_t* cycle, ngx_int_t id);


ngx_pid_t
ngx_spawn_process(ngx_cycle_t *cycle, char *name, ngx_int_t respawn)
{
    u_long          rc, n, code;
    ngx_int_t       s;
    ngx_pid_t       pid;
    ngx_exec_ctx_t  ctx;
    HANDLE          events[2];
    char            file[MAX_PATH + 1];

    if (respawn >= 0) {
        s = respawn;

    } else {
        for (s = 0; s < ngx_last_process; s++) {
            if (ngx_processes[s].handle == NULL) {
                break;
            }
        }

        if (s == NGX_MAX_PROCESSES) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "no more than %d processes can be spawned",
                          NGX_MAX_PROCESSES);
            return NGX_INVALID_PID;
        }
    }

    n = GetModuleFileName(NULL, file, MAX_PATH);

    if (n == 0) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "GetModuleFileName() failed");
        return NGX_INVALID_PID;
    }

    file[n] = '\0';

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                   "GetModuleFileName: \"%s\"", file);

    ctx.path = file;
    ctx.name = name;
    ctx.args = GetCommandLine();
    ctx.argv = NULL;
    ctx.envp = NULL;

    ///****************************************************************************
/// @author  : mxn                                                          
/// @file    :             
/// @brief   : several worker from skylar_srvcomp_win                                                                          
///****************************************************************************

    ngx_set_worker_id(cycle, s);    //****************   set NGX_WORKER_ID = s : int 

    pid = ngx_execute(cycle, &ctx);

    if (pid == NGX_INVALID_PID) {
        return pid;
    }

    ngx_memzero(&ngx_processes[s], sizeof(ngx_process_t));

    ngx_processes[s].handle = ctx.child;
    ngx_processes[s].pid = pid;
    ngx_processes[s].name = name;

    ngx_sprintf(ngx_processes[s].term_event, "ngx_%s_term_%P%Z", name, pid);
    ngx_sprintf(ngx_processes[s].quit_event, "ngx_%s_quit_%P%Z", name, pid);
    ngx_sprintf(ngx_processes[s].reopen_event, "ngx_%s_reopen_%P%Z",
                name, pid);

    events[0] = ngx_master_process_event;
    events[1] = ctx.child;


    //rc = WaitForMultipleObjects(2, events, 0, 5000);

///****************************************************************************
/// @author  : mxn                                                          
/// @file    :             
/// @brief   : several worker from skylar_srvcomp_win                                                                          
///****************************************************************************

    rc = WaitForMultipleObjects(2, events, 0, 60000);

    ngx_time_update();

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                   "WaitForMultipleObjects: %ul", rc);

    switch (rc) {

    case WAIT_OBJECT_0:

        ngx_processes[s].term = OpenEvent(EVENT_MODIFY_STATE, 0,
                                          (char *) ngx_processes[s].term_event);
        if (ngx_processes[s].term == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "OpenEvent(\"%s\") failed",
                          ngx_processes[s].term_event);
            goto failed;
        }

        ngx_processes[s].quit = OpenEvent(EVENT_MODIFY_STATE, 0,
                                          (char *) ngx_processes[s].quit_event);
        if (ngx_processes[s].quit == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "OpenEvent(\"%s\") failed",
                          ngx_processes[s].quit_event);
            goto failed;
        }

        ngx_processes[s].reopen = OpenEvent(EVENT_MODIFY_STATE, 0,
                                       (char *) ngx_processes[s].reopen_event);
        if (ngx_processes[s].reopen == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "OpenEvent(\"%s\") failed",
                          ngx_processes[s].reopen_event);
            goto failed;
        }

        if (ResetEvent(ngx_master_process_event) == 0) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "ResetEvent(\"%s\") failed",
                          ngx_master_process_event_name);
            goto failed;
        }

        break;

    case WAIT_OBJECT_0 + 1:
        if (GetExitCodeProcess(ctx.child, &code) == 0) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "GetExitCodeProcess(%P) failed", pid);
        }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "%s process %P exited with code %Xl",
                      name, pid, code);

        goto failed;

    case WAIT_TIMEOUT:
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "the event \"%s\" was not signaled for 5s",
                      ngx_master_process_event_name);
        goto failed;

    case WAIT_FAILED:
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "WaitForSingleObject(\"%s\") failed",
                      ngx_master_process_event_name);

        goto failed;
    }

    if (respawn >= 0) {
        return pid;
    }

    switch (respawn) {

    case NGX_PROCESS_RESPAWN:
        ngx_processes[s].just_spawn = 0;
        break;

    case NGX_PROCESS_JUST_RESPAWN:
        ngx_processes[s].just_spawn = 1;
        break;
    }

    if (s == ngx_last_process) {
        ngx_last_process++;
    }

    return pid;

failed:

    if (ngx_processes[s].reopen) {
        ngx_close_handle(ngx_processes[s].reopen);
    }

    if (ngx_processes[s].quit) {
        ngx_close_handle(ngx_processes[s].quit);
    }

    if (ngx_processes[s].term) {
        ngx_close_handle(ngx_processes[s].term);
    }

    ///****************************************************************************
    /// @author  : mxn                                                          
    /// @file    :             
    /// @brief   : several worker from skylar_srvcomp_win                                                                          
    ///****************************************************************************

    if (ngx_processes[s].handle) {
        TerminateProcess(ngx_processes[s].handle, 2);  //����ɱ�����̣�exit code 2
        ngx_close_handle(ngx_processes[s].handle);
        ngx_processes[s].handle = NULL;
    }

    //TerminateProcess(ngx_processes[s].handle, 2);

    if (ngx_processes[s].handle) {  //�����ߵ�
        ngx_close_handle(ngx_processes[s].handle);
        ngx_processes[s].handle = NULL;
    }

    return NGX_INVALID_PID;
}

///****************************************************************************
/// @author  : mxn                                                          
/// @file    :             
/// @brief   : several worker from skylar_srvcomp_win                                                                          
///****************************************************************************

void
ngx_set_worker_id(ngx_cycle_t* cycle, ngx_int_t id)
{
    char* var;
    u_char* p;

    var = ngx_alloc(NGX_INT32_LEN + 1, cycle->log);

    p = var;
    p = ngx_sprintf(p, "%d", (ngx_int_t)id);
    *p = '\0';

    SetEnvironmentVariable(NGX_WORKER_ID, var);

    ngx_free(var);

}

ngx_pid_t
ngx_execute(ngx_cycle_t *cycle, ngx_exec_ctx_t *ctx)
{
    STARTUPINFO          si;
    PROCESS_INFORMATION  pi;

    ngx_memzero(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);

    ngx_memzero(&pi, sizeof(PROCESS_INFORMATION));

    ///****************************************************************************
    /// @author  : mxn                                                          
    /// @file    :             
    /// @brief   : several worker from skylar_srvcomp_win
    /// create inherited process                                                                          
    ///****************************************************************************

    //if (CreateProcess(ctx->path, ctx->args,
    //                  NULL, NULL, 0, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)
    //    == 0)
    if (CreateProcess(ctx->path, ctx->args,
            NULL, NULL, 1, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)
            == 0)
    {
        ngx_log_error(NGX_LOG_CRIT, cycle->log, ngx_errno,
                      "CreateProcess(\"%s\") failed", ngx_argv[0]);

        return 0;
    }

    ctx->child = pi.hProcess;

    if (CloseHandle(pi.hThread) == 0) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "CloseHandle(pi.hThread) failed");
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "start %s process %P", ctx->name, pi.dwProcessId);

    return pi.dwProcessId;
}
