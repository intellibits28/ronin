# Product Guidelines: Ronin Kernel

## 1. Writing Style
**Educational & Direct:** Documentation and agent responses should be informative and easy to understand, providing direct answers while explaining underlying technical concepts when relevant.

## 2. Brand Personality
**Approachable Expert:** The Ronin Kernel acts as a knowledgeable guide that is powerful yet accessible, making edge AI and hardware-aware agency understandable for developers and power users alike.

## 3. User Experience (UX) Principles
*   **Performance-First:** Prioritize zero UI lag and extreme resource efficiency. Every millisecond saved in inference and IPC is a win for the user.
*   **Highly Interactive:** Leverage real-time command suggestions and proactive system health monitoring to keep the user informed and engaged with the kernel's state.
*   **Privacy & Sovereignty:** Empower users with transparent management of local files and model states, ensuring that "Local Inference-as-a-Service" means true data ownership.

## 4. Development Strategy
**Modular Core-First:** Focus on building robust, high-performance native C++20 modules for the reasoning spine and hardware bridge before layering high-level user interface elements. This ensures a stable and efficient foundation for future expansion.