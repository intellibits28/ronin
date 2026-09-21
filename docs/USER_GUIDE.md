# Ronin Kernel User Guide & Manual (အသုံးပြုသူ လမ်းညွှန်)

Welcome to the official user manual for **Ronin Kernel** — a sovereign, on-device cognitive AI agent and Structural Health Monitoring (SHM) runtime for Android.

Ronin Kernel ၏ အသုံးပြုသူလက်စွဲစာအုပ်မှ ကြိုဆိုပါသည်။ ဤလမ်းညွှန်တွင် Ronin ကို ထည့်သွင်းခြင်း၊ On-device Brain တင်ခြင်း၊ Cloud Providers ချိတ်ဆက်ခြင်း၊ အဆောက်အအုံတုန်ခါမှု တိုင်းတာခြင်း (SHM)၊ ရေရှည်မှတ်ဉာဏ် (LTM) ထိန်းသိမ်းခြင်းနှင့် Backup ရယူနည်းများကို အသေးစိတ် ရှင်းပြထားပါသည်။

---

## မာတိကာ (Table of Contents)

1. [စနစ်မိတ်ဆက် (System Overview)](#၁-စနစ်မိတ်ဆက်-system-overview)
2. [စတင်ထည့်သွင်းခြင်းနှင့် Setup (Installation & Initial Setup)](#၂-စတင်ထည့်သွင်းခြင်းနှင့်-setup-installation--initial-setup)
3. [Brain မော်ဒယ်များနှင့် Cloud စနစ်များ ချိတ်ဆက်ခြင်း (Models & Cloud Providers)](#၃-brain-မော်ဒယ်များနှင့်-cloud-စနစ်များ-ချိတ်ဆက်ခြင်း-models--cloud-providers)
4. [စကားပြောဆိုခြင်းနှင့် စဉ်းစားတွေးခေါ်မှုမှတ်တမ်း (Chat & Reasoning Logs)](#၄-စကားပြောဆိုခြင်းနှင့်-စဉ်းစားတွေးခေါ်မှုမှတ်တမ်း-chat--reasoning-logs)
5. [အဆောက်အအုံကြံ့ခိုင်မှု တိုင်းတာခြင်း (Structural Health Monitoring - SHM)](#၅-အဆောက်အအုံကြံ့ခိုင်မှု-တိုင်းတာခြင်း-structural-health-monitoring---shm)
6. [ဖုန်းစနစ်နှင့် Hardware ထိန်းချုပ်မှုများ (Device & Hardware Tools)](#၆-ဖုန်းစနစ်နှင့်-hardware-ထိန်းချုပ်မှုများ-device--hardware-tools)
7. [ရေရှည်မှတ်ဉာဏ်နှင့် လုံခြုံရေး Vault (Long-Term Memory & Vault)](#၇-ရေရှည်မှတ်ဉာဏ်နှင့်-လုံခြုံရေး-vault-long-term-memory--vault)
8. [မှတ်ဉာဏ် Backup ရယူခြင်းနှင့် အသစ်ပြန်တင်ခြင်း (Disaster Recovery & Backup)](#၈-မှတ်ဉာဏ်-backup-ရယူခြင်းနှင့်-အသစ်ပြန်တင်ခြင်း-disaster-recovery--backup)
9. [အမြန်သုံး Command များ (Slash Commands Reference)](#၉-အမြန်သုံး-command-များ-slash-commands-reference)
10. [မကြာခဏမေးလေ့ရှိသော မေးခွန်းများနှင့် ဖြေရှင်းနည်းများ (FAQ & Troubleshooting)](#၁၀-မကြာခဏမေးလေ့ရှိသော-မေးခွန်းများနှင့်-ဖြေရှင်းနည်းများ-faq--troubleshooting)

---

## ၁။ စနစ်မိတ်ဆက် (System Overview)

Ronin သည် အင်တာနက်မလိုဘဲ ဖုန်းထဲတွင် သီးသန့်အလုပ်လုပ်နိုင်သော Native C++20 Cognitive Microkernel ဖြစ်ပြီး Android စနစ်နှင့် တိုက်ရိုက် ပေါင်းစပ်ထားပါသည်။

### အဓိက စွမ်းဆောင်ရည်များ (Core Capabilities):
* **On-Device Local AI**: Google LiteRT-LM (Gemma 4) မော်ဒယ်များကို ဖုန်းထဲတွင် offline အပြည့်အဝ run နိုင်ခြင်း။
* **Hybrid Cloud Fallback**: Gemini, OpenAI, OpenRouter နှင့် မိမိစိတ်ကြိုက် Cloud API များကို အချိန်မရွေး ပြောင်းလဲအသုံးပြုနိုင်ခြင်း။
* **Structural Health Monitoring (SHM)**: 3-Axis Accelerometer နှင့် 2048-pt Welch Fast Fourier Transform (FFT) အသုံးပြု၍ အဆောက်အအုံ၊ တံတားနှင့် စက်ပစ္စည်းများ၏ တုန်ခါမှုကြိမ်နှုန်း (Resonance Frequency) ကို 0.05 Hz တိကျမှုဖြင့် တိုင်းတာစစ်ဆေးပေးနိုင်ခြင်း။
* **Device Control Tools**: ဖုန်းဓာတ်မီး (Flashlight)၊ Wi-Fi၊ Bluetooth၊ GPS တည်နေရာ၊ ဖိုင်ရှာဖွေခြင်း (File Search)၊ မက်ဆေ့ခ်ျ (SMS) ပို့ခြင်းများကို AI မှ အလိုအလျောက် ခိုင်းစေနိုင်ခြင်း။
* **Long-Term Memory (LTM)**: စကားပြောဆိုမှုများ၊ အရေးကြီး အချက်အလက် (Facts) များကို SQLite FTS5 lexical search ဖြင့် အမြဲတမ်း မှတ်သားထားနိုင်ခြင်း။
* **Disaster Recovery**: App ကို update သို့မဟုတ် reinstall လုပ်သည့်အခါ မူလမှတ်ဉာဏ်များ မပျောက်စေရန် တစ်ချက်နှိပ်ရုံဖြင့် Downloads ထဲသို့ Backup ထုတ်ပေးနိုင်ခြင်း။

---

## ၂။ စတင်ထည့်သွင်းခြင်းနှင့် Setup (Installation & Initial Setup)

### APK ထည့်သွင်းခြင်း
1. GitHub Releases သို့မဟုတ် GitHub Actions Artifact မှ `ronin-kernel-app-debug.apk` ကို ဒေါင်းလုဒ်ရယူပါ။
2. APK ကို ဖွင့်၍ "Install" (သို့မဟုတ် ရှိပြီးသားအပေါ်မှ "Update") ပြုလုပ်ပါ။
   *(မှတ်ချက် - Ronin တွင် ပုံသေ Deterministic Keystore သုံးထားသောကြောင့် နောင်ထွက်မည့် version များကို uninstall လုပ်စရာမလိုဘဲ အပေါ်ကနေ တိုက်ရိုက် Update တင်နိုင်ပါသည်)*

### လိုအပ်သော Permissions များ ခွင့်ပြုပေးခြင်း
App ကို ပထမဆုံးဖွင့်သည့်အခါ အောက်ပါ permission များကို လိုအပ်သလို ခွင့်ပြုပေးပါ-
* **Location (GPS)**: အနီးအနား တည်နေရာနှင့် မြေပုံအချက်အလက် ရယူရန်။
* **Camera / Flashlight**: ဓာတ်မီး အဖွင့်/အပိတ် ထိန်းချုပ်ရန်။
* **Storage / All Files Access**: ဒေသတွင်း ဖိုင်များကို ရှာဖွေနိုင်ရန်နှင့် Brain Model (.litertlm) များ တင်သွင်းရန်။
* **SMS / Contacts**: မက်ဆေ့ခ်ျပို့ခြင်းနှင့် ဖုန်းနံပါတ်ရှာဖွေရန် (ခွင့်ပြုချက်တောင်းခံသည့်အခါ အမြဲတမ်း အတည်ပြုချက် မေးမြန်းပါမည်)။

---

## ၃။ Brain မော်ဒယ်များနှင့် Cloud စနစ်များ ချိတ်ဆက်ခြင်း (Models & Cloud Providers)

Ronin သည် ဒေသတွင်း On-device Model သို့မဟုတ် Cloud Provider တစ်ခုခု (သို့မဟုတ် နှစ်ခုစလုံး) ဖြင့် အလုပ်လုပ်နိုင်ပါသည်။

### နည်းလမ်း (က) - Local Brain Model တင်သွင်းနည်း (.litertlm)
1. ဘယ်ဘက်အပေါ်ထောင့်ရှိ **Menu (☰)** ကို နှိပ်ပြီး Settings ထဲသို့ ဝင်ပါ။
2. **Section 4: Local Brain** သို့ ဆင်းပါ။
3. **Import Brain (.litertlm)** ခလုတ်ကို နှိပ်ပြီး ဖုန်းထဲရှိ LiteRT-LM Format မော်ဒယ်ဖိုင် (ဥပမာ - `gemma-2b-it.litertlm` သို့မဟုတ် `.bin`) ကို ရွေးချယ်ပေးပါ။
4. Model Hydration အောင်မြင်ပါက အစိမ်းရောင်ဖြင့် "Kernel Ready" ဖြစ်သွားပါမည်။

### နည်းလမ်း (ခ) - Cloud Provider (Gemini / OpenAI / OpenRouter) ချိတ်ဆက်နည်း
ဒေသတွင်း Local Model မရှိပါကလည်း Cloud စနစ်ဖြင့် အလွယ်တကူ သုံးနိုင်ပါသည်-
1. Settings > **Section 3: Cloud Providers** သို့ သွားပါ။
2. **Add Cloud Provider** ကို နှိပ်ပါ။
3. မိမိသုံးလိုသော အမျိုးအစား (Gemini, OpenAI, OpenRouter သို့မဟုတ် Custom) ကို ရွေးပါ။
4. မိမိ၏ **API Key** ကို ရိုက်ထည့်ပြီး သိမ်းဆည်းပါ။
5. **Ping** ခလုတ်ကို နှိပ်၍ ချိတ်ဆက်မှု အဆင်ပြေကြောင်း စမ်းသပ်နိုင်ပါသည်။
6. Settings > Section 1 တွင် **Cloud Only Mode** ကို ဖွင့်ထားပါက အင်တာနက်မှတစ်ဆင့်သာ တိုက်ရိုက် အမြန်ဆုံး ဉာဏ်ရည်တု အဖြေများကို ထုတ်ပေးပါမည်။

---

## ၄။ စကားပြောဆိုခြင်းနှင့် စဉ်းစားတွေးခေါ်မှုမှတ်တမ်း (Chat & Reasoning Logs)

* **ဘာသာစကား ပံ့ပိုးမှု**: Ronin သည် မြန်မာစာ (Unicode) နှင့် အင်္ဂလိပ်စာ နှစ်မျိုးစလုံးကို သဘာဝကျကျ နားလည်ပြီး ပြန်လည်ဖြေကြားပေးနိုင်ပါသည်။
* **Reasoning Console (အတွင်းပိုင်း တွေးခေါ်မှုပြသခြင်း)**:
  - Ronin သည် အဖြေမထုတ်မီ အတွင်းပိုင်းတွင် စဉ်းစားတွေးခေါ်မှု (Chain-of-Thought) ကို `[THINK] ... [/THINK]` block ဖြင့် ပြုလုပ်ပါသည်။
  - အပေါ်ဘက် TopBar ရှိ **မျက်လုံးပုံ Icon** ကို နှိပ်၍ အဆိုပါ တွေးခေါ်မှုအဆင့်ဆင့်ကို အချိန်နှင့်တပြေးညီ ကြည့်ရှုနိုင်ပါသည်။
* **ဆက်လက်ရေးသားခိုင်းခြင်း (Continue)**:
  - ရှည်လျားသော အဖြေများ ရေးသားနေစဉ် တုံ့ဆိုင်းသွားပါက ကတ်အောက်ခြေရှိ **"ဆက်ရေးပါ (Continue)"** ခလုတ်ကို နှိပ်၍ ဆက်လက် ရေးခိုင်းနိုင်ပါသည်။

---

## ၅။ အဆောက်အအုံကြံ့ခိုင်မှု တိုင်းတာခြင်း (Structural Health Monitoring - SHM)

Ronin တွင် ကမ္ဘာ့အဆင့်မီ တုန်ခါမှုဆိုင်ရာ အချက်ပြတွက်ချက်မှု (Modal Validation Engine v3 & Welch FFT) ပါဝင်ပါသည်။

### မည်သို့ တိုင်းတာရမည်နည်း?
1. ဖုန်းကို တုန်ခါမှုတိုင်းတာလိုသော ကြမ်းပြင်၊ စားပွဲ၊ တိုင် သို့မဟုတ် စက်ယန္တရားမျက်နှာပြင်ပေါ်တွင် လှုပ်ရှားမှုမရှိအောင် ငြိမ်သက်စွာ ချထားပါ။
2. စကားပြောဆိုမှုတွင် အောက်ပါအတိုင်း မေးမြန်း/ခိုင်းစေပါ-
   - `"တုန်ခါမှု စစ်ဆေးပေးပါ"`
   - `"အဆောက်အအုံ ကြံ့ခိုင်မှု စစ်ပေးပါ"`
   - `"Check structural vibration"`
3. Ronin သည် Accelerometer မှ အချက်အလက်များကို 100Hz ဖြင့် နမူနာ 1024 ခု (၁၀.၂ စက္ကန့်စာ) တိုင်းတာပြီး High-Pass Filter ဖြင့် မြေဆွဲအား (Gravity DC) ကို ဖယ်ရှားကာ Fast Fourier Transform (FFT) တွက်ချက်ပါမည်။
4. **ရလဒ်များတွင် အောက်ပါတို့ကို တွေ့ရပါမည်**:
   - **Dominant Resonance Frequency (Hz)**: အဓိက တုန်ခါကြိမ်နှုန်း (ဥပမာ - 4.5 Hz သို့မဟုတ် 12.8 Hz)။
   - **Quality Factor (Q)** & **SNR (dB)**: လှိုင်း၏ သန့်စင်မှုနှင့် ပြတ်သားမှု။
   - **Modal Confidence Score**: တုန်ခါမှု အချက်အလက် ခိုင်မာမှု ရာခိုင်နှုန်း။
   - **Multi-Axis Coherence**: X, Y, Z မျက်နှာပြင် ၃ ခုစလုံးတွင် တွေ့ရှိရမှု။
5. **Export ပြုလုပ်ခြင်း**:
   - ရလဒ်အောက်ခြေရှိ **"Export JSON"** သို့မဟုတ် **"Export Report"** ကို နှိပ်၍ အင်ဂျင်နီယာအစီရင်ခံစာအဖြစ် ထုတ်ယူနိုင်ပါသည်။

---

## ၆။ ဖုန်းစနစ်နှင့် Hardware ထိန်းချုပ်မှုများ (Device & Hardware Tools)

Ronin အား ဖုန်းတွင်း လုပ်ဆောင်ချက်များကို သာမန်စကားပြောသကဲ့သို့ ခိုင်းစေနိုင်ပါသည်-

| လုပ်ဆောင်ချက် | စမ်းသပ်ခိုင်းနိုင်သော စကားစုများ |
| :--- | :--- |
| **ဓာတ်မီး (Flashlight)** | `"ဓာတ်မီး ဖွင့်ပေးပါ"`, `"မီးပိတ်လိုက်တော့"`, `"Turn on flashlight"` |
| **Wi-Fi** | `"Wi-Fi ဖွင့်ပါ"`, `"ဝိုင်ဖိုင် ပိတ်လိုက်"`, `"Toggle WiFi"` |
| **Bluetooth** | `"Bluetooth ဖွင့်ပါ"`, `"ဘလူးတု ပိတ်ပေး"`, `"Turn on Bluetooth"` |
| **တည်နေရာ (GPS)** | `"ငါ အခု ဘယ်ရောက်နေလဲ"`, `"Show my location coordinates"` |
| **ဖိုင်ရှာဖွေခြင်း** | `"ဖုန်းထဲက PDF တွေ ရှာပေးပါ"`, `"Find invoice documents"` |
| **မက်ဆေ့ခ်ျ (SMS)** | `"0912345678 ကို နေကောင်းလားလို့ မက်ဆေ့ခ်ျပို့ပေးပါ"` *(ခွင့်ပြုချက်တောင်းခံပါမည်)* |
| **အဆက်အသွယ် (Contacts)** | `"U Ba ရဲ့ ဖုန်းနံပါတ် ရှာပေးပါ"`, `"Find contact John"` |

---

## ၇။ ရေရှည်မှတ်ဉာဏ်နှင့် လုံခြုံရေး Vault (Long-Term Memory & Vault)

* **မှတ်ဉာဏ် သိမ်းဆည်းခြင်း (Save Memory)**:
  - `"ငါ့မွေးနေ့က အောက်တိုဘာ ၂၀ ဖြစ်တယ် မှတ်ထားပေးပါ"`
  - `"Ronin, remember my office gate code is 9876"`
* **မှတ်ဉာဏ် ပြန်မေးခြင်း (Recall Memory)**:
  - `"ငါ့မွေးနေ့ ဘယ်တော့လဲ"`
  - `"What did I tell you about my office gate code?"`
* **လုံခြုံရေး Vault**: လျှို့ဝှက်ကုဒ်များနှင့် အရေးကြီး အချက်အလက်များကို AES-256 စနစ်ဖြင့် ကုဒ်ဝှက်သိမ်းဆည်းထားပြီး၊ လက်ဗွေ (Biometric) သို့မဟုတ် ဖုန်းလော့ခ်ကုဒ် အောင်မြင်မှသာ ပြန်လည်ထုတ်ပေးပါသည်။

---

## ၈။ မှတ်ဉာဏ် Backup ရယူခြင်းနှင့် အသစ်ပြန်တင်ခြင်း (Disaster Recovery & Backup)

App ကို ဖျက်လိုက်ရသည့်အခါ သို့မဟုတ် ဖုန်းအသစ်သို့ ပြောင်းရွှေ့သည့်အခါ မိမိ၏ Cognitive Memory များ မပျောက်ပျက်စေရန် အထူးပြုလုပ်ထားသော စနစ်ဖြစ်ပါသည်။

### Backup ယူနည်း (အလွန်လွယ်ကူပါသည်):
1. ဘယ်ဘက်အပေါ် Menu (☰) > Settings သို့ သွားပါ။
2. **Section 6: Cognitive Memory & Disaster Recovery** သို့ ဆင်းပါ။
3. **Backup DB** ခလုတ်ကို နှိပ်ပါ။
4. ဖုန်း၏ **Downloads** ဖိုဒါထဲသို့ `ronin_cognitive_backup.db` ဖိုင်အဖြစ် အလိုအလျောက် သိမ်းဆည်းပေးပါမည်။

### Backup ပြန်တင်နည်း (Restore):
1. Settings > Section 6 သို့ သွားပါ။
2. **Restore DB** ခလုတ်ကို နှိပ်ပြီး မိမိ backup ယူထားသော `.db` ဖိုင်ကို ရွေးချယ်ပေးလိုက်ရုံပင် ဖြစ်ပါသည်။
3. *(Auto-Recovery)*: App ကို clean install အသစ်ပြန်တင်သည့်အခါ ဖုန်း၏ Downloads ထဲတွင် `ronin_cognitive_backup.db` ရှိနေပါက App ဖွင့်သည်နှင့် အလိုအလျောက် မှတ်ဉာဏ်များကို ပြန်လည်ဆယ်ယူ (auto-restore) ပေးပါသည်။

---

## ၉။ အမြန်သုံး Command များ (Slash Commands Reference)

Chat Box ထဲတွင် အောက်ပါ command များကို တိုက်ရိုက်ရိုက်ထည့်၍ အမြန်စစ်ဆေးနိုင်ပါသည်-

* `/help` သို့မဟုတ် `/capabilities`: Ronin ၏ စွမ်းဆောင်ရည်များနှင့် သုံးနိုင်သော tool များကို အကျဉ်းချုပ် ပြသပေးခြင်း။
* `/status`: စနစ်၏ လက်ရှိ ကျန်းမာရေးအခြေအနေ၊ CPU အပူချိန်နှင့် RAM အသုံးပြုမှုကို စစ်ဆေးခြင်း။
* `/skills`: လက်ရှိ အလုပ်လုပ်နေသော Native Capability Nodes များကို ကြည့်ရှုခြင်း။
* `/model`: အသုံးပြုနေသော Brain Model ဖိုင်လမ်းကြောင်းကို စစ်ဆေးခြင်း။
* `/reset`: စကားပြောဆိုမှု မှတ်တမ်းနှင့် KV Cache ကို ရှင်းလင်း၍ Turn 1 သို့ ပြန်လည်စတင်ခြင်း။
* `/reflect`: ညဉ့်နက်ပိုင်း အလိုအလျောက် ပြုလုပ်သော Behavioral Reflection စက်ဝန်းကို ချက်ချင်း run စေခြင်း။

---

## ၁၀။ မကြာခဏမေးလေ့ရှိသော မေးခွန်းများနှင့် ဖြေရှင်းနည်းများ (FAQ & Troubleshooting)

#### မေး - GitHub Artifact APK ကို update တင်သည့်အခါ "App not installed" ပြနေပါသည်?
**ဖြေ** - ယခင် ဗားရှင်းဟောင်းများတွင် random key ဖြင့် sign လုပ်ထားခဲ့ပါက တစ်ကြိမ်သာ clean install ပြုလုပ်ရန် လိုအပ်ပါမည်။ မဖျက်မီ **Settings > Section 6 > Backup DB** ကို အရင်နှိပ်၍ memory သိမ်းထားပါ။ ထို့နောက် လက်ရှိ ဗားရှင်းအသစ်ကို သွင်းလိုက်ပါက auto-restore ဖြစ်သွားပါမည်။ နောက်နောင် ထွက်မည့် build အားလုံးသည် ပုံသေ signature သုံးထားသဖြင့် တိုက်ရိုက် update တင်နိုင်ပါပြီ။

#### မေး - Local Model မရှိဘဲ Ronin ကို သုံးနိုင်ပါသလား?
**ဖြေ** - သုံးနိုင်ပါသည်။ Settings ထဲတွင် Google Gemini, OpenAI သို့မဟုတ် OpenRouter API Key ထည့်သွင်းပြီး **Cloud Only Mode** ဖွင့်ထားပါက အင်တာနက်ဖြင့် အပြည့်အဝ အသုံးပြုနိုင်ပါသည်။

#### မေး - တုန်ခါမှုတိုင်းတာသည့်အခါ တိကျမှုရှိစေရန် မည်သို့ပြုလုပ်ရမည်နည်း?
**ဖြေ** - ဖုန်းကို ကိုင်မထားဘဲ တိုင်းတာလိုသော မျက်နှာပြင်ပေါ်တွင် ညီညာစွာ ငြိမ်သက်စွာ တင်ထားပြီးမှ စစ်ဆေးခိုင်းပါ။ စစ်ဆေးနေစဉ် ဖုန်းကို လှုပ်ရှားခြင်းမပြုပါနှင့်။
