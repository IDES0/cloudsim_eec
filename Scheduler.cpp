// Scheduler.cpp
// CloudSim
//
// Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Interfaces.h"
#include "Scheduler.hpp"
#include <unordered_set>
#include <algorithm>
#include <climits>



// Global state variables
//static bool migrating = true; 
// currently, we do not support migration.
static vector<VMId_t> vms;
static vector<MachineId_t> machines;
static unordered_map<TaskId_t, VMId_t> task_to_vm_map;
static unordered_map<MachineId_t, unsigned int> machine_task_count;
static unordered_set<VMId_t> migrating_vms;
static unsigned active_machines;



// Helper function to find less loaded machine
MachineId_t FindLessLoadedMachine(MachineId_t current_machine) {
    MachineId_t best_machine = current_machine;
    unsigned min_tasks = UINT_MAX;

    for (MachineId_t machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        if (machine_id != current_machine && 
            machine_info.active_tasks < min_tasks) {
            min_tasks = machine_info.active_tasks;
            best_machine = machine_id;
        }
    }

    return best_machine;
}

void Scheduler::Init() {
    // Get actual number of machines from the system
    unsigned total_machines = Machine_GetTotal();
    active_machines = total_machines;

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler with diverse machine types", 1);

    // Populate 'machines' vector with all MachineId_t
    for(unsigned i = 0; i < active_machines; i++) {
        MachineId_t machine_id = i;
        machines.push_back(machine_id);
    }

    // Create and attach VMs based on each machine's CPU type and GPU availability
    for(auto machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        VMType_t vm_type = (machine_info.cpu == POWER) ? AIX : LINUX;

        // Adjust VM creation based on GPU availability
        // If the machine has GPUs and the VM type supports it, create appropriate VM
        // Currently, GPU-enabled VMs are treated similarly; adjust if different VM types are required
        VMId_t vm_id = VM_Create(vm_type, machine_info.cpu);
        vms.push_back(vm_id);
        VM_Attach(vm_id, machine_id);
        SimOutput("Init(): VM " + to_string(vm_id) + " created and attached to Machine " + to_string(machine_id), 3);
    }
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    unsigned task_memory = GetTaskMemory(task_id); // Get memory requirement of the task

    // Set priority based on SLA with SLA2 elevated to balance all SLAs up to 80%
    Priority_t priority = LOW_PRIORITY;
    switch(task_info.required_sla) {
        case SLA0:
            priority = HIGH_PRIORITY; // Highest priority for SLA0
            break;
        case SLA1:
            priority = MID_PRIORITY;  // Medium priority for SLA1
            break;
        case SLA2:
            priority = MID_PRIORITY;  // Medium priority for SLA2
            break;
        case SLA3:
            priority = LOW_PRIORITY;   // Low priority for SLA3
            break;
        default:
            priority = LOW_PRIORITY;   // Default to low priority
    }

    // Identify eligible VMs based on compatibility and readiness
    vector<VMId_t> eligible_vms;
    for(auto vm_id : vms) {
        if(migrating_vms.find(vm_id) != migrating_vms.end()) {
            continue; // Skip migrating VMs
        }

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if(vm_info.vm_type != task_info.required_vm) {
            continue; // Skip incompatible VM types
        }

        if(vm_info.cpu != task_info.required_cpu) {
            continue; // Skip incompatible CPU types
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
            SimOutput("WakeUpMachineIfNeeded(): Waking up machine " + to_string(vm_info.machine_id), 3);
            Machine_SetState(vm_info.machine_id, S0);
            continue; // Skip if machine is not in S0 state
        }

        unsigned available_memory = machine_info.memory_size - machine_info.memory_used;
        if(available_memory < task_memory) {
            continue; // Not enough memory
        }

        eligible_vms.push_back(vm_id);
    }

    // Assign task to the eligible VM that can complete it the earliest
    VMId_t best_vm = -1;
    Time_t earliest_finish_time = UINT_MAX;

    for(auto vm_id : eligible_vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);

        // Calculate available MIPS based on current active tasks
        unsigned active_tasks = machine_info.active_tasks;
        unsigned cpu_count = machine_info.num_cpus;
        double mips = machine_info.performance[0];
        double available_mips = mips * cpu_count - (active_tasks * mips * 0.5);

        // dont divide by 0 lol
        if(available_mips <= 0) continue;

        // Calculate estimated runtime based on available MIPS
        double estimated_runtime = static_cast<double>(task_info.total_instructions) / available_mips;

        // Calculate if the task can be completed within its SLA deadline
        double sla_multiplier = 1.0;
        switch(task_info.required_sla) {
            case SLA0:
                sla_multiplier = 1.2;
                break;
            case SLA1:
                sla_multiplier = 1.5;
                break;
            case SLA2:
                sla_multiplier = 2.0;
                break;
            case SLA3:
                sla_multiplier = 3.0;
                break;
            default:
                sla_multiplier = 1.0;
        }
        Time_t sla_deadline = task_info.arrival + static_cast<Time_t>(task_info.target_completion * sla_multiplier);
        Time_t estimated_finish_time = now + static_cast<Time_t>(estimated_runtime);

        if(estimated_finish_time <= sla_deadline && estimated_finish_time < earliest_finish_time) {
            best_vm = vm_id;
            earliest_finish_time = estimated_finish_time;
        }
    }

    if(best_vm != -1) {
        VM_AddTask(best_vm, task_id, priority);
        task_to_vm_map[task_id] = best_vm;
        return;
    }

    // If no suitable VM found, attempt to create a new VM on a compatible machine
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
        VMType_t vm_type = task_info.required_vm;
        VMId_t new_vm = VM_Create(vm_type, task_info.required_cpu);
        VM_Attach(new_vm, target_machine);
        vms.push_back(new_vm);
        // Assign the task to the new VM
        VM_AddTask(new_vm, task_id, priority);
        task_to_vm_map[task_id] = new_vm;
        return;
    }

    // If still no suitable VM found, big bad
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
}

void Scheduler::PeriodicCheck(Time_t now) {

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

    // if (counts == 10 && !migrating) {
    //     migrating = true;
    //     VM_Migrate(1, 9);
    // }
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

}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}
