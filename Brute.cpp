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

        if (machine_id != current_machine && machine_info.active_tasks < min_tasks) {
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
    unsigned task_memory = GetTaskMemory(task_id);

    // Set priority based on SLA
    Priority_t priority = LOW_PRIORITY;
    switch(task_info.required_sla) {
        case SLA0: priority = HIGH_PRIORITY; break;
        case SLA1: case SLA2: priority = MID_PRIORITY; break;
        default: priority = LOW_PRIORITY;
    }

    // Brute force: Try every possible VM/machine combination
    VMId_t best_vm = -1;
    double best_score = -1;

    for(auto vm_id : vms) {
        // Skip migrating VMs
        if(migrating_vms.find(vm_id) != migrating_vms.end()) continue;

        VMInfo_t vm_info = VM_GetInfo(vm_id);
        MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);

        // Basic compatibility checks
        if (vm_info.vm_type != task_info.required_vm ||
            vm_info.cpu != task_info.required_cpu ||
            (task_info.gpu_capable && !machine_info.gpus) ||
            (machine_info.memory_size - machine_info.memory_used) < task_memory) {
            continue;
        }

        // Wake up machine if needed
        if(machine_info.s_state != S0) {
            Machine_SetState(vm_info.machine_id, S0);
            continue;
        }

        // Calculate performance score
        double available_mips = machine_info.performance[0] * machine_info.num_cpus - 
                              (machine_info.active_tasks * machine_info.performance[0] * 0.5);
        if(available_mips <= 0) continue;

        double estimated_runtime = static_cast<double>(task_info.total_instructions) / available_mips;
        
        // Calculate SLA deadline
        double sla_multiplier = (task_info.required_sla == SLA0) ? 1.2 :
                              (task_info.required_sla == SLA1) ? 1.5 :
                              (task_info.required_sla == SLA2) ? 2.0 : 3.0;
        
        Time_t sla_deadline = task_info.arrival + static_cast<Time_t>(task_info.target_completion * sla_multiplier);
        Time_t estimated_finish_time = now + static_cast<Time_t>(estimated_runtime);

        // Score calculation - lower is better
        double score = estimated_finish_time;
        
        // Penalties for various factors
        if(machine_info.active_tasks > 0) score *= 1.1;  // Slight penalty for busy machines
        if(machine_info.memory_used > machine_info.memory_size * 0.8) score *= 1.2;  // Memory pressure penalty
        
        // If we can meet SLA and this is the best score so far
        if(estimated_finish_time <= sla_deadline && (best_vm == -1 || score < best_score)) {
            best_vm = vm_id;
            best_score = score;
        }
    }

    // If we found a suitable VM, assign the task
    if(best_vm != -1) {
        VM_AddTask(best_vm, task_id, priority);
        task_to_vm_map[task_id] = best_vm;
        return;
    }

    // If no suitable VM found, create new VM on best available machine
    MachineId_t best_machine = -1;
    double best_machine_score = -1;

    for(auto machine_id : machines) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id);

        // Basic compatibility checks
        if(machine_info.cpu != task_info.required_cpu ||
           (task_info.gpu_capable && !machine_info.gpus) ||
           (machine_info.memory_size - machine_info.memory_used) < task_memory) {
            continue;
        }

        // Score calculation for machines
        double score = machine_info.performance[0] * machine_info.num_cpus;
        score /= (machine_info.active_tasks + 1);  // Account for current load
        
        if(best_machine == -1 || score > best_machine_score) {
            best_machine = machine_id;
            best_machine_score = score;
        }
    }

    if(best_machine != -1) {
        VMId_t new_vm = VM_Create(task_info.required_vm, task_info.required_cpu);
        VM_Attach(new_vm, best_machine);
        vms.push_back(new_vm);
        VM_AddTask(new_vm, task_id, priority);
        task_to_vm_map[task_id] = new_vm;
    }
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
