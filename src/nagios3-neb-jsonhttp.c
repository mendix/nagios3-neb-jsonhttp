/*
 *
 * Copyright (c) 2012, Mendix bv
 * All Rights Reserved.
 *
 * http://www.mendix.com/
 *
 */

#ifndef NSCORE
# define NSCORE
#endif

/*Event Broker header files*/
#include "nagios3-dev/nebmodules.h"
#include "nagios3-dev/nebcallbacks.h"
#include "nagios3-dev/nebstructs.h"
#include "nagios3-dev/broker.h"

/*Nagios header files*/
#include "nagios3-dev/config.h"
#include "nagios3-dev/common.h"
#include "nagios3-dev/nagios.h"
#include "nagios3-dev/objects.h"

//CURL
#include "curl/curl.h"

//JSON
#include "json/json.h"

NEB_API_VERSION(CURRENT_NEB_API_VERSION);

static int callbackHandler(int callback_type, void *data);
int handleNotificationData( nebstruct_notification_data *data );
int handleServiceCheckData( nebstruct_service_check_data *data );
int handleHostCheckData( nebstruct_host_check_data *data );
int handleFlappingData( nebstruct_flapping_data *data );
int handleStateChangeData( nebstruct_statechange_data *data );

static char *argument;

int nebmodule_init(int flags, char *args, nebmodule *handle)
{
    if(args != NULL) {
        if(strlen(args) <= 2000)
        {
            char text[4096];
            argument = args;
            snprintf(text, 4096, "I will use %s to send information.", args);
            text[sizeof(text)-1]='\0';
            write_to_log(text, NSLOG_INFO_MESSAGE, NULL);
        } else {
            argument = "http://localhost";
            write_to_log("Argument is too long, falling back to http://localhost to send information.", NSLOG_INFO_MESSAGE, NULL);
        }
    } else {
        argument = "http://localhost";
        write_to_log("No argument passed, falling back to http://localhost to send information.", NSLOG_INFO_MESSAGE, NULL);
    }

    neb_register_callback(NEBCALLBACK_NOTIFICATION_DATA, handle, 0, callbackHandler);
    neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, handle, 0, callbackHandler);
    neb_register_callback(NEBCALLBACK_HOST_CHECK_DATA, handle, 0, callbackHandler);
    neb_register_callback(NEBCALLBACK_FLAPPING_DATA, handle, 0, callbackHandler);
    neb_register_callback(NEBCALLBACK_STATE_CHANGE_DATA, handle, 0, callbackHandler);

    return 0;
}

int nebmodule_deinit(int flags, int reason)
{
    return 0;
}

static int callbackHandler(int callback_type, void *data)
{
    if(data == NULL)
    {
        return 0;
    }

    switch(callback_type)
    {
        case NEBCALLBACK_NOTIFICATION_DATA:
            handleNotificationData((nebstruct_notification_data *)data);
            break;
        case NEBCALLBACK_SERVICE_CHECK_DATA:
            handleServiceCheckData((nebstruct_service_check_data *)data);
            break;
        case NEBCALLBACK_HOST_CHECK_DATA:
            handleHostCheckData((nebstruct_host_check_data *)data);
            break;
        case NEBCALLBACK_FLAPPING_DATA:
            handleFlappingData((nebstruct_flapping_data *)data);
            break;
        case NEBCALLBACK_STATE_CHANGE_DATA:
            handleStateChangeData((nebstruct_statechange_data *)data);
            break;
        default :
            write_to_log("DEFAULT", NSLOG_INFO_MESSAGE, NULL);

        // Always return OK (zero) for succes. Although the call-back return code
        // is currently ignored by Nagios, it may be utilized in the future.
        return 0;

    }
}

int handleNotificationData(nebstruct_notification_data *data)
{
    struct json_object *jsonobj, *sendjsonobj;
    jsonobj = json_object_new_object();
    json_object_object_add(jsonobj, "type", json_object_new_int(data->type));
    json_object_object_add(jsonobj, "flags", json_object_new_int(data->flags));
    json_object_object_add(jsonobj, "attr", json_object_new_int(data->attr));
    json_object_object_add(jsonobj, "timestamp", json_object_new_int(data->timestamp.tv_sec));
    json_object_object_add(jsonobj, "notification_type", json_object_new_int(data->notification_type));
    json_object_object_add(jsonobj, "start_time", json_object_new_int(data->start_time.tv_sec));
    json_object_object_add(jsonobj, "end_time", json_object_new_int(data->end_time.tv_sec));
    json_object_object_add(jsonobj, "host_name", json_object_new_string(data->host_name == NULL ? "" : data->host_name));
    json_object_object_add(jsonobj, "service_description", json_object_new_string(data->service_description == NULL ? "" : data->service_description));
    json_object_object_add(jsonobj, "reason_type", json_object_new_int(data->type));
    json_object_object_add(jsonobj, "state", json_object_new_int(data->type));
    json_object_object_add(jsonobj, "output", json_object_new_string(data->output == NULL ? "" : data->output));
    json_object_object_add(jsonobj, "ack_author", json_object_new_string(data->ack_author == NULL ? "" : data->ack_author));
    json_object_object_add(jsonobj, "ack_data", json_object_new_string(data->ack_data == NULL ? "" : data->ack_data));
    json_object_object_add(jsonobj, "escalated", json_object_new_int(data->type));
    json_object_object_add(jsonobj, "contacts_notified", json_object_new_int(data->type));

    if(data->notification_type == SERVICE_NOTIFICATION)
    {
        struct service_struct *svc = data->object_ptr;
        json_object_object_add(jsonobj, "svc_current_state", json_object_new_int(svc->current_state));
    }

    if(data->notification_type == HOST_NOTIFICATION)
    {
        struct host_struct *hst = data->object_ptr;
        json_object_object_add(jsonobj, "hst_current_state", json_object_new_int(hst->current_state));
    }

    sendjsonobj = json_object_new_object();
    json_object_object_add(sendjsonobj, "neb_notification_data", jsonobj);

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, argument);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        struct curl_slist *slist = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(sendjsonobj));

        res = curl_easy_perform(curl);
        curl_slist_free_all(slist);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    json_object_put(jsonobj);
    json_object_put(sendjsonobj);

    return 0;
}

int handleServiceCheckData(nebstruct_service_check_data *data)
{
    // I only want complete finished service checks to send information
    if(data->type == NEBTYPE_SERVICECHECK_PROCESSED)
    {
        struct json_object *jsonobj, *sendjsonobj;
        jsonobj = json_object_new_object();
        json_object_object_add(jsonobj, "type", json_object_new_int(data->type));
        json_object_object_add(jsonobj, "flags", json_object_new_int(data->flags));
        json_object_object_add(jsonobj, "attr", json_object_new_int(data->attr));
        json_object_object_add(jsonobj, "timestamp", json_object_new_int(data->timestamp.tv_sec));
        json_object_object_add(jsonobj, "host_name", json_object_new_string(data->host_name == NULL ? "" : data->host_name));
        json_object_object_add(jsonobj, "service_description", json_object_new_string(data->service_description == NULL ? "" : data->service_description));
        json_object_object_add(jsonobj, "check_type", json_object_new_int(data->check_type));
        json_object_object_add(jsonobj, "current_attempt", json_object_new_int(data->current_attempt));
        json_object_object_add(jsonobj, "max_attempts", json_object_new_int(data->max_attempts));
        json_object_object_add(jsonobj, "state_type", json_object_new_int(data->state_type));
        json_object_object_add(jsonobj, "state", json_object_new_int(data->state));
        json_object_object_add(jsonobj, "timeout", json_object_new_int(data->timeout));
        json_object_object_add(jsonobj, "command_name", json_object_new_string(data->command_name == NULL ? "" : data->command_name));
        json_object_object_add(jsonobj, "command_args", json_object_new_string(data->command_args == NULL ? "" : data->command_args));
        json_object_object_add(jsonobj, "command_line", json_object_new_string(data->command_line == NULL ? "" : data->command_line));
        json_object_object_add(jsonobj, "start_time", json_object_new_int(data->start_time.tv_sec));
        json_object_object_add(jsonobj, "end_time", json_object_new_int(data->end_time.tv_sec));
        json_object_object_add(jsonobj, "early_timeout", json_object_new_int(data->early_timeout));
        json_object_object_add(jsonobj, "execution_time", json_object_new_double(data->execution_time));
        json_object_object_add(jsonobj, "latency", json_object_new_double(data->latency));
        json_object_object_add(jsonobj, "return_code", json_object_new_int(data->return_code));
        json_object_object_add(jsonobj, "output", json_object_new_string(data->output == NULL ? "" : data->output));
        json_object_object_add(jsonobj, "perf_data", json_object_new_string(data->perf_data == NULL ? "" : data->perf_data));

        struct service_struct *svc = data->object_ptr;
        json_object_object_add(jsonobj, "svc_last_check", json_object_new_int(svc->last_check));
        json_object_object_add(jsonobj, "svc_next_check", json_object_new_int(svc->next_check));
        json_object_object_add(jsonobj, "svc_last_state_change", json_object_new_int(svc->last_state_change));
        json_object_object_add(jsonobj, "svc_last_notification", json_object_new_int(svc->last_notification));
        json_object_object_add(jsonobj, "svc_next_notification", json_object_new_int(svc->next_notification));
        json_object_object_add(jsonobj, "svc_last_time_ok", json_object_new_int(svc->last_time_ok));
        json_object_object_add(jsonobj, "svc_last_time_warning", json_object_new_int(svc->last_time_warning));
        json_object_object_add(jsonobj, "svc_last_time_unknown", json_object_new_int(svc->last_time_unknown));
        json_object_object_add(jsonobj, "svc_last_time_critical", json_object_new_int(svc->last_time_critical));
        json_object_object_add(jsonobj, "svc_is_flapping", json_object_new_int(svc->is_flapping));
        json_object_object_add(jsonobj, "svc_percent_state_change", json_object_new_double(svc->percent_state_change));
        json_object_object_add(jsonobj, "svc_display_name", json_object_new_string(svc->display_name));

        sendjsonobj = json_object_new_object();
        json_object_object_add(sendjsonobj, "neb_service_check_data", jsonobj);

        CURL *curl;
        CURLcode res;

        curl = curl_easy_init();
        if(curl)
        {

            curl_easy_setopt(curl, CURLOPT_URL, argument);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            struct curl_slist *slist = curl_slist_append(NULL, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(sendjsonobj));

            res = curl_easy_perform(curl);
            curl_slist_free_all(slist);
            curl_easy_cleanup(curl);
        }

        curl_global_cleanup();
        json_object_put(jsonobj);
        json_object_put(sendjsonobj);
    }

    return 0;
}

int handleHostCheckData(nebstruct_host_check_data *data)
{
    // I only want complete finished host checks to send information
    if(data->type == NEBTYPE_HOSTCHECK_PROCESSED)
    {
        struct json_object *jsonobj, *sendjsonobj;
        jsonobj = json_object_new_object();
        json_object_object_add(jsonobj, "type", json_object_new_int(data->type));
        json_object_object_add(jsonobj, "flags", json_object_new_int(data->flags));
        json_object_object_add(jsonobj, "attr", json_object_new_int(data->attr));
        json_object_object_add(jsonobj, "timestamp", json_object_new_int(data->timestamp.tv_sec));
        json_object_object_add(jsonobj, "host_name", json_object_new_string(data->host_name == NULL ? "" : data->host_name));
        json_object_object_add(jsonobj, "current_attempt", json_object_new_int(data->current_attempt));
        json_object_object_add(jsonobj, "check_type", json_object_new_int(data->check_type));
        json_object_object_add(jsonobj, "max_attempts", json_object_new_int(data->max_attempts));
        json_object_object_add(jsonobj, "state_type", json_object_new_int(data->state_type));
        json_object_object_add(jsonobj, "state", json_object_new_int(data->state));
        json_object_object_add(jsonobj, "timeout", json_object_new_int(data->timeout));
        json_object_object_add(jsonobj, "command_name", json_object_new_string(data->command_name == NULL ? "" : data->command_name));
        json_object_object_add(jsonobj, "command_args", json_object_new_string(data->command_args == NULL ? "" : data->command_args));
        json_object_object_add(jsonobj, "command_line", json_object_new_string(data->command_line == NULL ? "" : data->command_line));
        json_object_object_add(jsonobj, "start_time", json_object_new_int(data->start_time.tv_sec));
        json_object_object_add(jsonobj, "end_time", json_object_new_int(data->end_time.tv_sec));
        json_object_object_add(jsonobj, "early_timeout", json_object_new_int(data->early_timeout));
        json_object_object_add(jsonobj, "execution_time", json_object_new_double(data->execution_time));
        json_object_object_add(jsonobj, "latency", json_object_new_double(data->latency));
        json_object_object_add(jsonobj, "return_code", json_object_new_int(data->return_code));
        json_object_object_add(jsonobj, "output", json_object_new_string(data->output == NULL ? "" : data->output));
        json_object_object_add(jsonobj, "perf_data", json_object_new_string(data->perf_data == NULL ? "" : data->perf_data));

        struct host_struct *hst = data->object_ptr;
        json_object_object_add(jsonobj, "hst_last_check", json_object_new_int(hst->last_check));
        json_object_object_add(jsonobj, "hst_last_state_change", json_object_new_int(hst->last_state_change));
        json_object_object_add(jsonobj, "hst_last_host_notification", json_object_new_int(hst->last_host_notification));
        json_object_object_add(jsonobj, "hst_next_host_notification", json_object_new_int(hst->next_host_notification));
        json_object_object_add(jsonobj, "hst_is_flapping", json_object_new_int(hst->is_flapping));
        json_object_object_add(jsonobj, "hst_percent_state_change", json_object_new_double(hst->percent_state_change));
        json_object_object_add(jsonobj, "hst_next_check", json_object_new_int(hst->next_check));

        sendjsonobj = json_object_new_object();
        json_object_object_add(sendjsonobj, "neb_host_check_data", jsonobj);

        CURL *curl;
        CURLcode res;

        curl = curl_easy_init();
        if(curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, argument);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            struct curl_slist *slist = curl_slist_append(NULL, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(sendjsonobj));

            res = curl_easy_perform(curl);
            curl_slist_free_all(slist);
            curl_easy_cleanup(curl);
        }

        curl_global_cleanup();
        json_object_put(jsonobj);
        json_object_put(sendjsonobj);
    }

    return 0;
}

int handleFlappingData(nebstruct_flapping_data *data)
{
    struct json_object *jsonobj, *sendjsonobj;
    jsonobj = json_object_new_object();
    json_object_object_add(jsonobj, "type", json_object_new_int(data->type));
    json_object_object_add(jsonobj, "flags", json_object_new_int(data->flags));
    json_object_object_add(jsonobj, "attr", json_object_new_int(data->attr));
    json_object_object_add(jsonobj, "timestamp", json_object_new_int(data->timestamp.tv_sec));
    json_object_object_add(jsonobj, "flapping_type", json_object_new_int(data->flapping_type));
    json_object_object_add(jsonobj, "host_name", json_object_new_string(data->host_name == NULL ? "" : data->host_name));
    json_object_object_add(jsonobj, "service_description", json_object_new_string(data->service_description == NULL ? "" : data->service_description));
    json_object_object_add(jsonobj, "percent_change", json_object_new_double(data->percent_change));
    json_object_object_add(jsonobj, "high_threshold", json_object_new_double(data->high_threshold));
    json_object_object_add(jsonobj, "low_threshold", json_object_new_double(data->low_threshold));
    json_object_object_add(jsonobj, "comment_id", json_object_new_double(data->comment_id));

    sendjsonobj = json_object_new_object();
    json_object_object_add(sendjsonobj, "neb_flapping_data", jsonobj);

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, argument);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        struct curl_slist *slist = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(sendjsonobj));

        res = curl_easy_perform(curl);
        curl_slist_free_all(slist);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    json_object_put(jsonobj);
    json_object_put(sendjsonobj);

    return 0;
}

int handleStateChangeData(nebstruct_statechange_data *data)
{
    struct json_object *jsonobj, *sendjsonobj;
    jsonobj = json_object_new_object();
    json_object_object_add(jsonobj, "type", json_object_new_int(data->type));
    json_object_object_add(jsonobj, "flags", json_object_new_int(data->flags));
    json_object_object_add(jsonobj, "attr", json_object_new_int(data->attr));
    json_object_object_add(jsonobj, "timestamp", json_object_new_int(data->timestamp.tv_sec));
    json_object_object_add(jsonobj, "statechange_type", json_object_new_int(data->statechange_type));
    json_object_object_add(jsonobj, "host_name", json_object_new_string(data->host_name == NULL ? "" : data->host_name));
    json_object_object_add(jsonobj, "service_description", json_object_new_string(data->service_description == NULL ? "" : data->service_description));
    json_object_object_add(jsonobj, "state", json_object_new_int(data->state));
    json_object_object_add(jsonobj, "state_type", json_object_new_int(data->state_type));
    json_object_object_add(jsonobj, "current_attempt", json_object_new_int(data->current_attempt));
    json_object_object_add(jsonobj, "max_attempts", json_object_new_int(data->max_attempts));
    json_object_object_add(jsonobj, "output", json_object_new_string(data->output == NULL ? "" : data->output));

    sendjsonobj = json_object_new_object();
    json_object_object_add(sendjsonobj, "neb_state_change_data", jsonobj);

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, argument);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        struct curl_slist *slist = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(sendjsonobj));

        res = curl_easy_perform(curl);
        curl_slist_free_all(slist);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    json_object_put(jsonobj);
    json_object_put(sendjsonobj);

    return 0;
}
