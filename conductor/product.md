# Product Guide: Ronin Kernel

## 1. Project Overview
**Ronin Kernel** is a modular, high-efficiency AI agent runtime optimized for Android (Snapdragon 778G+). It bridges pure C++20 reasoning spines with Kotlin hardware bridges.

## 2. Target Audience
The project targets Android developers, power users, and edge AI researchers who require efficient, local AI execution on mobile hardware without cloud reliance.

## 3. Core Value Proposition
**Native Hardware Integration:** Ronin Kernel provides seamless hardware-accelerated classification (via NPU), bridging complex C++ logic and Android components for a highly performant and secure edge AI experience.

## 4. Key Features
*   **Advanced Memory Management:** Features tri-anchor pruning (L1-RAM, L2-Cache, L3-SQLite) combined with memory-mapped I/O (mmap) persistence for instant loading and minimal RAM usage.
*   **Terminal Command Intelligence:** Integrates a real-time suggester and auto-completion for `/` terminal commands, enabling fast and intuitive user interaction.
*   **System Health Guards:** Employs active thermal throttling (Safe Mode triggered at 42°C) and LMK-aware memory pruning to ensure device stability during heavy inference tasks.

## 5. Future Vision
**Sentient Adaptive Agent:** The ultimate goal is to evolve the kernel into a self-improving, hardware-aware agent that dynamically adapts to user behavior and real-time thermal/memory conditions.