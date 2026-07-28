# 1. Executive Summary

## System Overview
Ronin is a sovereign mobile AI runtime designed for Android edge devices. At its core, Ronin bridges the gap between high-performance native systems programming and dynamic, reasoning-based AI execution. The project operates independently of continuous cloud connectivity, relying on an isolated C++20 kernel for decision-making and a Kotlin/JNI Android shell for hardware interactions.

## Key Capabilities
- **Native Cognitive Loop**: A C++ engine that continuously observes, plans, and executes actions without relying on a JVM-bound main thread.
- **On-Device Inference**: Integration with LiteRT-LM (formerly TensorFlow Lite) to run models like Gemma 4 E2B completely on-device, preserving privacy and reducing latency.
- **Structural Health Monitoring (SHM)**: A highly-specialized module that processes raw accelerometer data, extracts modal frequencies (F₀) with <2% CV repeatability, and validates structural integrity using deterministic math combined with AI reasoning.

## Target Environments
Ronin is built for environments where privacy, offline reliability, and processing speed are paramount. It is specifically optimized for Android Edge devices, but gracefully degrades to use Cloud LLMs (Gemini, OpenRouter) when local constraints are exceeded or when the user explicitly requests higher-tier reasoning power.
