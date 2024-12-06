//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//
#include "Interfaces.h"
#include "Scheduler.hpp"
#include <unordered_set>
#include <algorithm>
static bool migrating = false;
// static unsigned active_machines = 16;

static vector<VMId_t> vms;
static vector<MachineId_t> machines;
static unordered_map<TaskId_t, VMId_t> task_to_vm_map;
static unordered_map<MachineId_t, unsigned int> machine_task_count;
static unordered_set<VMId_t> migrating_vms;
static unsigned active_machines;
void Scheduler::Init() {
    // Get actual number of machines from the system
    unsigned total_machines = Machine_GetTotal();
    active_machines = total_machines;  // Use actual total instead of hardcoded 16

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    // Create VMs and track machines based on actual total
    for(unsigned i = 0; i < active_machines; i++) {
        vms.push_back(VM_Create(LINUX, X86));
        machines.push_back(MachineId_t(i));
    }    

    // Attach VMs to machines
    for(unsigned i = 0; i < active_machines; i++) {
        VM_Attach(vms[i], machines[i]);
    }
}


void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    
    // Turn on high performance mode for the destination machine
    Machine_SetCorePerformance(vm_info.machine_id, 0, P0);
    
    // Update task mappings
    for(auto& pair : task_to_vm_map) {
        if(pair.second == vm_id) {
            pair.second = vm_id;  // Update to new VM location
        }
    }
}

void WakeUpMachineIfNeeded(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);

    // Check if the machine is in any state other than S0
    if (machine_info.s_state != S0) {
        SimOutput("WakeUpMachineIfNeeded(): Waking up machine " + to_string(machine_id), 3);
        Machine_SetState(machine_id, S0);
    }
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    unsigned task_memory = GetTaskMemory(task_id); // Get memory requirement of the task

    // Set priority based on SLA
    Priority_t priority = (task_info.required_sla == SLA0 || task_info.required_sla == SLA1) 
                         ? HIGH_PRIORITY 
                         : (task_info.required_sla == SLA2 ? MID_PRIORITY : LOW_PRIORITY);

    VMId_t selected_vm = -1;
    unsigned min_available_memory = UINT_MAX;

    // Find the best-fit VM that is ready and has enough memory
    for (VMId_t vm_id : vms) {
        if (migrating_vms.find(vm_id) != migrating_vms.end()) {
            continue;  // Skip migrating VMs
        }

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);

        // Ensure the machine is in S0 state
        if (machine_info.s_state != S0) {
            WakeUpMachineIfNeeded(vm_info.machine_id);
            continue;  // Skip this VM for now
        }

        // Calculate available memory on the machine
        unsigned available_memory = machine_info.memory_size - machine_info.memory_used;
        if (available_memory >= task_memory) {
            unsigned remaining_memory = available_memory - task_memory;
            if (remaining_memory < min_available_memory) {
                min_available_memory = remaining_memory;
                selected_vm = vm_id;
            }
        }
    }

    // Assign the task to the best-fit VM if found
    if (selected_vm != -1) {
        try {
            VM_AddTask(selected_vm, task_id, priority);
            task_to_vm_map[task_id] = selected_vm;
            SimOutput("NewTask(): Task " + to_string(task_id) + " assigned to VM " + to_string(selected_vm), 3);
            return;
        } catch (const runtime_error& e) {
            SimOutput("NewTask(): Exception assigning task " + to_string(task_id) +
                      " to VM " + to_string(selected_vm) + ": " + e.what(), 2);
        }
    }

    // If no suitable VM found, attempt to create a new VM on a ready machine with enough memory
    for (MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        // Ensure the machine is ready and has enough memory
        if (machine_info.s_state == S0 && 
            (machine_info.memory_size - machine_info.memory_used) >= task_memory) {
            try {
                VMId_t new_vm = VM_Create(LINUX, machine_info.cpu);
                VM_Attach(new_vm, machine_id);
                vms.push_back(new_vm);
                migrating_vms.insert(new_vm); // Mark the new VM as migrating to prevent immediate assignment

                // Assign the task to the new VM after ensuring it's ready
                VM_AddTask(new_vm, task_id, priority);
                task_to_vm_map[task_id] = new_vm;
                migrating_vms.erase(new_vm); // VM is now ready
                SimOutput("NewTask(): Task " + to_string(task_id) + " assigned to new VM " + to_string(new_vm) + " on Machine " + to_string(machine_id), 3);
                return;
            } catch (const runtime_error& e) {
                SimOutput("NewTask(): Exception assigning task " + to_string(task_id) +
                          " to new VM on Machine " + to_string(machine_id) + ": " + e.what(), 2);
            }
        }
    }

    // If still no suitable VM found, log failure
    SimOutput("NewTask(): No suitable VM found for task " + to_string(task_id), 0);
}




MachineId_t FindLessLoadedMachine(MachineId_t current_machine) {
    MachineId_t best_machine = current_machine;
    unsigned min_tasks = UINT_MAX;

    for (MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        if (machine_id != current_machine && machine_info.active_tasks < min_tasks) {
            min_tasks = machine_info.active_tasks;
            best_machine = machine_id;
        }
    }

    return best_machine;
}

bool CheckSLAViolation(TaskId_t task_id, Time_t now) {
    // Get task information
    TaskInfo_t task_info = GetTaskInfo(task_id);

    // A task violates its SLA if it's not completed and past the target completion time
    if (!task_info.completed && now > task_info.target_completion) {
        return true; // SLA violation
    }
    return false; // SLA is still met
}


void Scheduler::PeriodicCheck(Time_t now) {
    for (VMId_t vm_id : vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);

        for (TaskId_t task_id : vm_info.active_tasks) {
            if (CheckSLAViolation(task_id, now)) {
                MachineId_t current_machine = vm_info.machine_id;

                // Find a less-loaded machine
                MachineId_t target_machine = FindLessLoadedMachine(current_machine);

                if (target_machine != current_machine &&
                    migrating_vms.find(vm_id) == migrating_vms.end()) {
                    VM_Migrate(vm_id, target_machine);
                    migrating_vms.insert(vm_id);
                    SimOutput("PeriodicCheck(): Initiated migration of VM " + to_string(vm_id) +
                              " to machine " + to_string(target_machine), 3);
                }
            }
        }
    }

    // Proactively wake up machines if needed
    for (MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);
        if (machine_info.s_state != S0 && machine_info.active_tasks == 0) {
            WakeUpMachineIfNeeded(machine_id);
        }
    }
}




void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    auto it = task_to_vm_map.find(task_id);
    if(it != task_to_vm_map.end()) {
        task_to_vm_map.erase(it);
    }
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // Log migration completion
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " completed at time " + to_string(time), 4);

    // Remove from migrating set
    migrating_vms.erase(vm_id);

    // Complete any additional migration steps (e.g., task updates)
    Scheduler.MigrationComplete(time, vm_id);
}


void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
    static unsigned counts = 0;
    counts++;
    if (counts == 10 && !migrating) {
        migrating = true;
        VM_Migrate(1, 9);
    }
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    
    // Escalate priority if SLA is violated
    Priority_t new_priority = task_info.priority;
    if (task_info.required_sla == SLA0 || task_info.required_sla == SLA1) {
        new_priority = HIGH_PRIORITY;
    } else if (task_info.required_sla == SLA2) {
        new_priority = MID_PRIORITY;
    } else {
        new_priority = HIGH_PRIORITY; // Upgrade SLA3 to higher priority on warning
    }
    
    // Update task priority
    SetTaskPriority(task_id, new_priority);
    
    // Attempt to migrate VM if possible
    auto it = task_to_vm_map.find(task_id);
    if (it != task_to_vm_map.end()) {
        VMId_t vm_id = it->second;
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        MachineId_t current_machine = vm_info.machine_id;
        
        // Find a machine with lower load
        MachineId_t target_machine = FindLessLoadedMachine(current_machine);
        
        if (target_machine != current_machine && migrating_vms.find(vm_id) == migrating_vms.end()) {
            VM_Migrate(vm_id, target_machine);
            migrating_vms.insert(vm_id);
            SimOutput("SLAWarning(): Initiated migration of VM " + to_string(vm_id) +
                      " to machine " + to_string(target_machine) + " due to SLA violation", 3);
        }
    }
}


void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}