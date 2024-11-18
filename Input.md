# 1. High-Performance Workload with Mixed SLAs
machine class:
{
    Number of machines: 24
    CPU type: X86
    Number of cores: 16
    Memory: 32768
    S-States: [150, 120, 100, 80, 40, 10, 0]
    P-States: [15, 12, 8, 4]
    C-States: [15, 4, 1, 0]
    MIPS: [2400, 1800, 1200, 600]
    GPUs: yes
}

task class:
{
    Start time: 0
    End time: 500000
    Inter arrival: 2000
    Expected runtime: 80000
    Memory: 4096
    VM type: LINUX
    GPU enabled: yes
    SLA type: SLA0
    CPU type: X86
    Task type: AI
    Seed: 123456
}

task class:
{
    Start time: 100000
    End time: 600000
    Inter arrival: 5000
    Expected runtime: 120000
    Memory: 8192
    VM type: LINUX
    GPU enabled: yes
    SLA type: SLA1
    CPU type: X86
    Task type: HPC
    Seed: 789012
}

# 2. Energy Efficiency Test
machine class:
{
    Number of machines: 12
    CPU type: ARM
    Number of cores: 4
    Memory: 8192
    S-States: [80, 60, 40, 20, 10, 5, 0]
    P-States: [8, 6, 4, 2]
    C-States: [8, 2, 1, 0]
    MIPS: [1000, 800, 500, 300]
    GPUs: no
}

task class:
{
    Start time: 0
    End time: 1000000
    Inter arrival: 30000
    Expected runtime: 40000
    Memory: 512
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA2
    CPU type: ARM
    Task type: WEB
    Seed: 345678
}

# 3. Heterogeneous Environment Test
machine class:
{
    Number of machines: 8
    CPU type: X86
    Number of cores: 8
    Memory: 16384
    S-States: [120, 100, 80, 60, 30, 5, 0]
    P-States: [12, 9, 6, 3]
    C-States: [12, 3, 1, 0]
    MIPS: [1600, 1200, 800, 400]
    GPUs: yes
}

machine class:
{
    Number of machines: 8
    CPU type: ARM
    Number of cores: 16
    Memory: 16384
    S-States: [100, 80, 60, 40, 20, 5, 0]
    P-States: [10, 7, 5, 2]
    C-States: [10, 2, 1, 0]
    MIPS: [1400, 1000, 700, 300]
    GPUs: no
}

task class:
{
    Start time: 0
    End time: 800000
    Inter arrival: 4000
    Expected runtime: 60000
    Memory: 2048
    VM type: LINUX
    GPU enabled: yes
    SLA type: SLA1
    CPU type: X86
    Task type: CRYPTO
    Seed: 901234
}

task class:
{
    Start time: 200000
    End time: 1000000
    Inter arrival: 6000
    Expected runtime: 50000
    Memory: 1024
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA2
    CPU type: ARM
    Task type: STREAM
    Seed: 567890
}

# Documentation

## General Notes
* Different seeds are used for each task class to ensure diverse yet reproducible workload patterns
* Memory values are carefully scaled to match real-world scenarios
* Power states are designed to provide meaningful energy-saving opportunities

## Scenario 1: High-Performance Workload with Mixed SLAs
### Machine Configuration
* Number of Machines: 24
    * Large number of machines to handle high-performance computing demands
    * Provides enough capacity for workload spikes
* CPU Type: X86
    * Chosen for high-performance computing capabilities
* Number of cores: 16
    * High core count for parallel processing of AI and HPC workloads
* Memory: 32768 MB (32 GB)
    * Large memory capacity to handle memory-intensive AI tasks
    * Prevents memory bottlenecks during peak loads
* Power States:
    * S-States: [150, 120, 100, 80, 40, 10, 0]
        * Higher power consumption reflecting enterprise-grade hardware
    * P-States: [15, 12, 8, 4]
        * Wide range for flexible performance/power tradeoffs
    * C-States: [15, 4, 1, 0]
        * Multiple idle states for energy optimization
* MIPS: [2400, 1800, 1200, 600]
    * High performance scaling for demanding workloads
* GPU: Yes
    * Required for AI and HPC acceleration

### Task Configurations
#### First Task Class (AI Workload)
* Timing:
    * Start: 0, End: 500000
    * Inter arrival: 2000 (frequent arrivals)
* Runtime: 80000
    * Moderate length for AI processing tasks
* Memory: 4096 MB
    * Sufficient for ML model processing
* SLA: SLA0 (strict)
    * Tests scheduler's ability to maintain performance under tight constraints

#### Second Task Class (HPC Workload)
* Timing:
    * Start: 100000 (delayed start)
    * End: 600000
    * Inter arrival: 5000
* Runtime: 120000
    * Longer runtime for HPC tasks
* Memory: 8192 MB
    * Large memory requirement for computational tasks
* SLA: SLA1
    * Slightly relaxed SLA for longer-running tasks

## Scenario 2: Energy Efficiency Test
### Machine Configuration
* Number of Machines: 12
    * Moderate cluster size for energy efficiency testing
* CPU Type: ARM
    * Selected for power efficiency
* Number of cores: 4
    * Balanced for web workloads
* Memory: 8192 MB
    * Sufficient for web serving
* Power States:
    * Lower power consumption across all states
    * Optimized for energy efficiency
* GPU: No
    * Not needed for web workloads

### Task Configuration
* Sparse workload pattern:
    * Long inter-arrival time (30000)
    * Moderate runtime (40000)
* Memory: 512 MB
    * Light memory footprint
* SLA: SLA2
    * Relaxed SLA suitable for non-critical web services

## Scenario 3: Heterogeneous Environment Test
### Machine Configurations
#### First Machine Class (X86)
* 8 machines with high-performance characteristics
* GPU-enabled for specialized workloads
* Balanced power/performance characteristics

#### Second Machine Class (ARM)
* 8 machines with efficiency-focused design
* Higher core count (16) but lower power consumption
* Optimized for stream processing workloads

### Task Configurations
#### CRYPTO Tasks
* Moderate arrival rate (4000)
* GPU-enabled for acceleration
* Memory: 2048 MB for cryptographic operations
* SLA1 for balanced performance requirements

#### STREAM Tasks
* Delayed start (200000) to test dynamic adaptation
* Regular arrivals (6000)
* Lower memory requirements (1024 MB)
* SLA2 for stream processing flexibility

## Testing Coverage
This configuration tests:
1. SLA compliance under various workload conditions
2. Energy efficiency optimization opportunities
3. Resource allocation in heterogeneous environments
4. Scheduler's ability to handle multiple concurrent workload types
5. Power state management across different hardware architectures

## DISCLAIMER: Used ChatGPT to generate various aspects of the input file, such as the documentation and structure provided above SLA requirements and metrics.
