# 🧪 Ronin Kernel: Beta Testing Guide (v3.6)

Welcome to the Ronin Kernel Beta! This guide will help you set up and test the sovereign AI runtime on your Android device.

## 🚀 Getting Started

### 1. Download & Install
-   Download the latest **Ronin Kernel Debug APK** from the [GitHub Actions Artifacts](https://github.com/intellibits28/ronin/actions) or the Releases page (if available).
-   Install the APK on your Android device. (You may need to enable "Install from Unknown Sources").

### 2. Download a Reasoning Spine (Gemma 4)
Ronin requires a local model to function in offline mode. Choose the model based on your device's RAM:

| Device RAM | Recommended Model | Download Link |
| :--- | :--- | :--- |
| **12GB+** | **Gemma 4 E4B** (Highest Quality) | [Download E4B](https://huggingface.co/litert-community/gemma-4-E4B-it-litert-lm/resolve/main/gemma-4-E4B-it.litertlm) |
| **8GB** | **Gemma 4 E2B** (Balanced Performance) | [Download E2B](https://huggingface.co/litert-community/gemma-4-E2B-it-litert-lm/resolve/main/gemma-4-E2B-it.litertlm) |

### 3. Import the Model
1.  Open the Ronin app.
2.  Open the Navigation Drawer (swipe from left or tap the menu icon).
3.  Tap **"Import Brain"**.
4.  Select the `.litertlm` file you downloaded in Step 2.
5.  Wait for the "Validating..." process to complete.

---

## ☁️ Cloud Provider Setup
If you want to use Cloud models or fetch dynamic models:
1.  Go to the Navigation Drawer -> **Add Cloud Provider**.
2.  Select your provider (e.g., **Gemini**, **OpenAI**, **OpenRouter**).
3.  Enter your **API Key**.
4.  Tap the **Refresh (Icon)** next to Model ID to fetch available models.
5.  Select your preferred model and tap **SAVE**.

---

## 🛠️ Basic Usage & Commands

### Chatting
-   Simply type a message and press send.
-   **Gemma 4** will stream its reasoning inside `[THINK]` blocks (Cyan logs) and its reply in the chat bubble.

### Slash Commands
Type these in the chat input for quick kernel actions:
-   `/status` : Check Kernel health, Thermal, and RAM usage.
-   `/reset` : Clear chat history and reset the local model's KV-cache.
-   `/model` : See details of the currently active model.
-   `/skills` : List active hardware nodes and capabilities.

---

## 🐞 Reporting Issues
Since this is a Beta, you might encounter bugs. Please report them on our [GitHub Issues](https://github.com/intellibits28/ronin/issues) page.

**Common Debugging:**
-   If the app hangs, use `/reset` to clear the context.
-   Check the **Reasoning Logs** switch in the drawer to monitor internal kernel activity.

Thank you for helping us harden the Ronin Kernel! 🧠⚡
