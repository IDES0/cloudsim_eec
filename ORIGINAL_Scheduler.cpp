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
    active_machines = total_machines;  // Use actual total instead of hardcoded value

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler with diverse machine types", 1);

    // Populate 'machines' vector with all MachineId_t
    // Assuming MachineId_t can be retrieved sequentially from 0 to total_machines - 1
    for(unsigned i = 0; i < active_machines; i++) {
        MachineId_t machine_id = i; // Ensure this correctly represents the actual machine IDs
        machines.push_back(machine_id);
    }

    // Create and attach VMs based on each machine's CPU type and GPU availability
    for(auto machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        VMType_t vm_type;
        switch(machine_info.cpu) {
            case X86:
                vm_type = LINUX;
                break;
            case ARM:
                vm_type = LINUX; // Assuming LINUX for ARM; adjust if needed
                break;
            case POWER:
                vm_type = AIX;
                break;
            default:
                vm_type = LINUX; // Default to LINUX if CPU type is unrecognized
        }

        try {
            VMId_t vm_id = VM_Create(vm_type, machine_info.cpu);
            vms.push_back(vm_id);
            VM_Attach(vm_id, machine_id);
            SimOutput("Init(): VM " + to_string(vm_id) + " created and attached to Machine " + to_string(machine_id), 3);
        } catch (const runtime_error& e) {
            SimOutput("Init(): Exception creating VM for Machine " + to_string(machine_id) +
                      ": " + e.what(), 2);
        }
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
    Priority_t priority = LOW_PRIORITY;
    if(task_info.required_sla == SLA0) {
        priority = HIGH_PRIORITY;
    }
    else if(task_info.required_sla == SLA1) {
        priority = MID_PRIORITY;
    }
    else if(task_info.required_sla == SLA2) {
        priority = MID_PRIORITY;
    }
    // SLA3 remains LOW_PRIORITY

    // Categorize VMs based on compatibility and readiness
    vector<VMId_t> high_priority_vms;
    vector<VMId_t> mid_priority_vms;
    vector<VMId_t> low_priority_vms;

    for(auto vm_id : vms) {
        if(migrating_vms.find(vm_id) != migrating_vms.end()) {
            continue; // Skip migrating VMs
        }

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        
        // Check VM type and CPU compatibility
        if(vm_info.vm_type != task_info.required_vm || vm_info.cpu != task_info.required_cpu) {
            continue; // Skip incompatible VMs
        }

        // Check GPU capability if required
        if(task_info.gpu_capable) {
            MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);
            if(!machine_info.gpus) {
                continue; // Skip VMs on machines without GPUs
            }
        }

        MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);
        if(machine_info.s_state != S0) {
            WakeUpMachineIfNeeded(vm_info.machine_id);
            continue; // Skip if machine is not in S0 state
        }

        unsigned available_memory = machine_info.memory_size - machine_info.memory_used;
        if(available_memory < task_memory) {
            continue; // Not enough memory
        }

        // Categorize VMs based on current priority
        if(priority == HIGH_PRIORITY) {
            high_priority_vms.push_back(vm_id);
        }
        else if(priority == MID_PRIORITY) {
            mid_priority_vms.push_back(vm_id);
        }
        else {
            low_priority_vms.push_back(vm_id);
        }
    }

    // Function to assign task to a list of VMs
    auto assign_task = [&](const vector<VMId_t>& vm_list) -> bool {
        // Best-Fit: Assign to VM with least remaining memory after assignment
        VMId_t best_vm = -1;
        unsigned least_remaining = UINT_MAX;

        for(auto vm_id : vm_list) {
            MachineInfo_t machine_info = Machine_GetInfo(VM_GetInfo(vm_id).machine_id);
            unsigned remaining_memory = machine_info.memory_size - machine_info.memory_used - task_memory;
            if(remaining_memory < least_remaining) {
                least_remaining = remaining_memory;
                best_vm = vm_id;
            }
        }

        if(best_vm != -1) {
            try {
                VM_AddTask(best_vm, task_id, priority);
                task_to_vm_map[task_id] = best_vm;
                SimOutput("NewTask(): Task " + to_string(task_id) + " assigned to VM " + to_string(best_vm), 3);
                return true;
            }
            catch(const runtime_error& e) {
                SimOutput("NewTask(): Exception assigning task " + to_string(task_id) +
                          " to VM " + to_string(best_vm) + ": " + e.what(), 2);
                return false;
            }
        }
        return false;
    };

    // Attempt to assign based on priority
    if(priority == HIGH_PRIORITY) {
        if(assign_task(high_priority_vms)) return;
    }
    if(priority == MID_PRIORITY) {
        if(assign_task(mid_priority_vms)) return;
    }
    if(priority == LOW_PRIORITY) {
        if(assign_task(low_priority_vms)) return;
    }

    // If not assigned yet, attempt to assign to any suitable VM
    vector<VMId_t> all_suitable_vms = high_priority_vms;
    all_suitable_vms.insert(all_suitable_vms.end(), mid_priority_vms.begin(), mid_priority_vms.end());
    all_suitable_vms.insert(all_suitable_vms.end(), low_priority_vms.begin(), low_priority_vms.end());

    if(assign_task(all_suitable_vms)) return;

    // If no suitable VM found, attempt to create a new VM on a compatible machine
    // Prioritize machines with GPUs if the task requires it
    bool gpu_required = task_info.gpu_capable;
    MachineId_t target_machine = -1;
    unsigned max_available_memory = 0;

    for(auto machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        // Ensure machine is ready
        if(machine_info.s_state != S0) {
            continue;
        }

        // Check CPU type
        if(machine_info.cpu != task_info.required_cpu) {
            continue;
        }

        // Check GPU capability if required
        if(gpu_required && !machine_info.gpus) {
            continue;
        }

        unsigned available_memory = machine_info.memory_size - machine_info.memory_used;
        if(available_memory >= task_memory && available_memory > max_available_memory) {
            max_available_memory = available_memory;
            target_machine = machine_id;
        }
    }

    if(target_machine != -1) {
        try {
            VMId_t new_vm = VM_Create(task_info.required_vm, task_info.required_cpu);
            VM_Attach(new_vm, target_machine);
            vms.push_back(new_vm);
            migrating_vms.insert(new_vm); // Mark as migrating to prevent immediate assignment

            // Optionally, you can wait for MigrationDone to assign the task
            // For simplicity, assigning immediately assuming VM is ready after Attach
            VM_AddTask(new_vm, task_id, priority);
            task_to_vm_map[task_id] = new_vm;
            migrating_vms.erase(new_vm); // VM is now ready
            SimOutput("NewTask(): Task " + to_string(task_id) + " assigned to new VM " + to_string(new_vm) +
                      " on Machine " + to_string(target_machine), 3);
            return;
        }
        catch(const runtime_error& e) {
            SimOutput("NewTask(): Exception assigning task " + to_string(task_id) +
                      " to new VM on Machine " + to_string(target_machine) + ": " + e.what(), 2);
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