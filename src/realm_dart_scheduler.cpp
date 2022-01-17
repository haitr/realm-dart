////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2021 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <sstream>
#include <set>
#include <mutex>
#include <thread>

#include <realm.h>
#include "dart_api_dl.h"
#include "realm_dart_scheduler.h"

struct SchedulerData {
    std::thread::id threadId;
    Dart_Port port;
    uint64_t isolateId;

    realm_scheduler_notify_func_t callback = nullptr;
    void* callback_userData = nullptr;
    realm_free_userdata_func_t free_userData_func = nullptr;

    SchedulerData(uint64_t isolate, Dart_Port dartPort) : port(dartPort), threadId(std::this_thread::get_id()), isolateId(isolate)
    {}
};

static const int SCHEDULER_FINALIZE = NULL;

void realm_dart_scheduler_free_userData(void* userData) {
    SchedulerData* schedulerData = static_cast<SchedulerData*>(userData);
    Dart_PostInteger_DL(schedulerData->port, SCHEDULER_FINALIZE);
    
    if (schedulerData->callback_userData != nullptr) {
        //call the function that will free the callback user data
        schedulerData->free_userData_func(schedulerData->callback_userData);
    }

    //delete the scheduler
    delete schedulerData;
}

//This can be invoked on any thread.
void realm_dart_scheduler_notify(void* userData) {
    auto& schedulerData = *static_cast<SchedulerData*>(userData);
    std::uintptr_t pointer = reinterpret_cast<std::uintptr_t>(userData);
    Dart_PostInteger_DL(schedulerData.port, pointer);
}

bool realm_dart_scheduler_is_on_thread(void* userData) {
    auto& schedulerData = *static_cast<SchedulerData*>(userData);
    return schedulerData.threadId == std::this_thread::get_id();
}

bool realm_dart_scheduler_is_same_as(const void* userData1, const void* userData2) {
    return userData1 == userData2;
}

bool realm_dart_scheduler_can_deliver_notifications(void* userData) {
    return true;
}

void realm_dart_scheduler_set_notify_callback(void* userData, void* callback_userData, realm_free_userdata_func_t free_userData_func, realm_scheduler_notify_func_t notify_func) {
    auto& schedulerData = *static_cast<SchedulerData*>(userData);
    schedulerData.callback = notify_func;
    schedulerData.callback_userData = callback_userData;
    schedulerData.free_userData_func = free_userData_func;
}

RLM_API realm_scheduler_t* realm_dart_create_scheduler(uint64_t isolateId, Dart_Port port) {
    SchedulerData* schedulerData = new SchedulerData(isolateId, port);

    realm_scheduler_t* realm_scheduler = realm_scheduler_new(schedulerData, 
        realm_dart_scheduler_free_userData, 
        realm_dart_scheduler_notify, 
        realm_dart_scheduler_is_on_thread, 
        realm_dart_scheduler_is_same_as,
        realm_dart_scheduler_can_deliver_notifications,
        realm_dart_scheduler_set_notify_callback);

    return realm_scheduler;
}

//This is called from Dart on the main thread
RLM_API void realm_dart_scheduler_invoke(uint64_t isolateId, void* userData) {
    SchedulerData* schedulerData = static_cast<SchedulerData*>(userData);

    if (schedulerData->callback == nullptr) {
        return;
    }

    //Isolates can change their underlying native threads!!!
    //We check if the invoking isolate is the same as the one that created this scheduler and update the thread id if it changed.
    //This is safe since we care about being on the same Isolate rather than being on the same thread id. 
    //Core asks the scheduler if this is on the correct thread so we need both ids
    if (schedulerData->isolateId == isolateId && schedulerData->threadId != std::this_thread::get_id()) {
        schedulerData->threadId = std::this_thread::get_id();
    }

    //invoke the notify callback
    schedulerData->callback(schedulerData->callback_userData);
}

RLM_API uint64_t get_thread_id() {
    std::stringstream ss;
    std::thread k;
    ss << std::this_thread::get_id();
    uint64_t id = std::stoull(ss.str());
    return id;
}