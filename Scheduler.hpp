//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//
#include "Interfaces.h"
#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <limits.h>
#include <vector>
#include <unordered_map>


class Scheduler {
public:
    Scheduler()                 {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
private:
    vector<VMId_t> vms;
    vector<MachineId_t> machines;
    bool CheckSLAViolation(TaskId_t task_id, Time_t now);
    void RotateVMs();
};



#endif /* Scheduler_hpp */