# Elara Virtual Machine Dual License Draft

**DRAFT ONLY — NOT LEGAL ADVICE**

This document is an early drafting framework intended for legal review and refinement by qualified counsel. It is designed to express the intended licensing philosophy and restrictions surrounding the Elara Virtual Machine ecosystem, specifically separating the open-source implementation from the protected instruction set architecture (ISA) and compatibility model.

---

# Elara Software License Structure

The Elara ecosystem is divided into two distinct licensing domains:

1. The Open Software Layer
2. The Protected ISA and Compatibility Layer

The intention of this structure is to permit broad software usage, modification, and ecosystem participation while preserving commercial control over the Elara Virtual Machine instruction set architecture, compatibility standards, and derivative execution environments.

---

# Part I — Open Software Layer

Unless otherwise stated, the following components are licensed under the MIT License:

- Runtime source code
- Libraries
- SDKs
- Tooling
- Build systems
- Editors and utilities
- Development frameworks
- Example applications
- User interface components
- Documentation unrelated to ISA implementation details
- Non-ISA helper modules

These components may be used, copied, modified, merged, published, distributed, sublicensed, and/or sold under the terms of the MIT License.

The intent is to encourage ecosystem growth, interoperability, educational use, experimentation, and commercial adoption of the software tooling itself.

---

# Part II — Protected ISA and Compatibility Layer

The following elements are expressly excluded from the MIT License and remain proprietary intellectual property:

- Elara Virtual Machine Instruction Set Architecture (ISA)
- Opcode specifications
- Instruction semantics
- Execution semantics
- Binary encoding formats
- Bytecode standards
- Compatibility specifications
- Execution model definitions
- Runtime behavior guarantees
- ISA extension mechanisms
- Hardware execution mappings
- Compatibility certification standards
- Virtual machine architectural specifications
- Protected protocol layers directly tied to ISA compatibility

These protected components are collectively referred to as the “Protected Architecture.”

No rights are granted to reproduce, reimplement, emulate, synthesize, manufacture, commercially host, or create derivative compatible execution systems implementing the Protected Architecture except as explicitly permitted under a separate written commercial agreement.

---

# Permitted Activities

Subject to compliance with all other license terms, the following activities are permitted:

- Use of the official Elara software implementation
- Modification of the official implementation for internal use
- Educational and research use
- Development of applications targeting the official runtime
- Creation of plugins and extensions that operate through approved interfaces
- Internal deployment of the official runtime
- Non-commercial experimentation using the official implementation
- Inspection and modification of MIT-licensed source code components

---

# Restricted Activities

Without explicit written authorization from the rights holder, the following activities are prohibited:

## 1. Independent Compatible Virtual Machines

Creating, distributing, or commercially operating any software runtime intended to:

- execute Elara-compatible bytecode,
- maintain ISA compatibility,
- replicate Elara execution semantics,
- or present itself as compatible with the Elara VM ecosystem.

This includes clean-room implementations.

---

## 2. Hardware and Silicon Implementations

Creating or distributing:

- FPGA implementations,
- ASIC implementations,
- hardware accelerators,
- microcode implementations,
- embedded processors,
- silicon derivatives,
- instruction decoders,
- or hardware execution environments

that implement or substantially replicate the Protected Architecture.

---

## 3. Commercial Hosted Runtime Services

Providing hosted services, cloud runtimes, execution environments, or virtualized infrastructure implementing the Protected Architecture without commercial authorization.

---

## 4. Derivative ISA Standards

Creating derivative instruction set standards, modified compatibility layers, or alternative execution standards substantially based on the Protected Architecture.

---

## 5. Compatibility Claims

No entity may claim:

- “Elara Compatible”
- “Compatible with Elara VM”
- “Elara ISA Compatible”
- or similar compatibility branding

without written authorization and, where applicable, certification.

---

# Trademark Reservation

All trademarks, service marks, logos, names, and compatibility branding associated with the Elara ecosystem remain the exclusive property of their respective owner.

No rights to trademarks are granted under the MIT License or any related agreement.

Use of Elara branding in commerce, certification, compatibility advertising, or derivative systems requires separate written authorization.

---

# Internal Modification Exception

Entities may modify the official implementation internally for operational purposes provided that:

- the modified system remains derived from the official implementation,
- no independent compatible implementation is created,
- and no distribution occurs in violation of this agreement.

This clause is intended to permit operational flexibility while preserving control over the architecture itself.

---

# Research Exception

Academic and non-commercial security research relating to the Elara implementation is permitted provided that:

- such activity does not create a commercially deployable compatible implementation,
- does not distribute substitute runtimes,
- and does not distribute hardware reproductions of the Protected Architecture.

---

# Ownership

All rights, title, and interest in the Protected Architecture remain exclusively with the rights holder.

Nothing in the MIT-licensed software layer shall be interpreted as granting rights to:

- the ISA,
- compatibility standards,
- hardware implementation rights,
- certification rights,
- or commercial reimplementation rights.

---

# Compatibility Licensing

Commercial licenses may be granted separately for:

- independent runtime implementations,
- hardware implementations,
- FPGA implementations,
- silicon manufacturing,
- compatibility certification,
- cloud deployment,
- embedded deployment,
- and derivative execution environments.

Such licenses shall be governed by separate contractual agreements.

---

# Defensive Intent

The purpose of this licensing structure is:

- to preserve ecosystem consistency,
- prevent fragmentation,
- maintain execution compatibility integrity,
- protect long-term architectural stability,
- and ensure that derivative execution environments remain contractually governed.

The intention is not to restrict application development or ecosystem participation, but rather to preserve governance over the execution substrate itself.

---

# Severability

If any portion of this agreement is determined unenforceable under applicable law, the remaining provisions shall continue in full force and effect.

---

# Jurisdiction

This agreement shall be governed by the applicable laws chosen by the rights holder in the final commercial form of the agreement.

---

# Final Notes for Legal Review

The following areas require formal legal analysis before adoption:

- ISA copyright enforceability by jurisdiction
- Patent strategy
- Trademark registration strategy
- Clean-room implementation handling
- Reverse engineering provisions
- Competition law considerations
- Open-source compatibility implications
- Commercial certification language
- Cloud execution enforceability
- FPGA and microcode enforceability
- Export and international licensing considerations
- Contributor license agreement structure
- Community contribution governance

This document is intended as a conceptual legal framework and drafting reference only.

