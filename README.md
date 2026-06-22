# FPGA Accelerator Project

## Toolchain Versions

* **Vitis HLS 2022.2** — Used for accelerator source code development and synthesis.
* **Vivado 2022.2** — Used for hardware integration and bitstream generation.
* **SDK (Final Release 2026)** — Used for making the iso file for the FPGAs

---

## Repository Contents

### 📄 HWSW_INTERFACE

Documentation describing the Hardware–Software interface of the accelerator, including:

* Parameter descriptions
* Input and output buffer requirements
* Software-to-hardware communication flow


---

## 📁 C_code

Contains software examples, templates, and support libraries required for accelerator integration.

### EXA_RECECCA_SW_TEMPLATE

Example Exapsys software project demonstrating accelerator usage through a simple Vector Addition (VADD) implementation.

**Purpose:**

* Reference software implementation
* Buffer allocation and execution flow demonstration

---

### EXA_LIBRARIES_FOR_TEMPLATE

Collection of Exapsys support libraries used by the software template.

**Features:**

* Input/output buffer creation
* Memory management utilities
* Hardware communication helpers

---

### FORTH_AI_SW_FROM_EXA_TEMPLATE

FORTH-developed software interface layer derived from the Exapsys template.

**Purpose:**

* Provides accelerator-specific software interfaces
* Handles communication between application software and hardware accelerator

---

## Design Files

### design.bitstream

FPGA bitstream generated using **Vivado 2022.2**.

This file contains the synthesized and implemented hardware design ready for FPGA programming.

---

## Development Flow

```text
Vitis HLS 2022.2
        │
        ▼
Accelerator IP Generation
        │
        ▼
Vivado 2022.2
        │
        ▼
design.bitstream
        │
        ▼
SDK (Final Release 2026)
        │
        ▼
FPGA  Execution
```

---

## Notes


