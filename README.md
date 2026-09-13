# DE Learner frontend

Qt/QML application using Felgo. Open `CMakeLists.txt` in the Qt Creator shipped
with Felgo. The root repository orchestrates backend and integration tests;
this page covers the Android application build.

## Android prerequisites

Use a consistent Felgo Qt 6.8.3 Android kit and its matching macOS host tools.
The installed kit used for this build has:

- Android ABI: `arm64-v8a` (use an ARM64 device or emulator).
- Android SDK platform: API 35; NDK: `27.2.12479018` (r27c).
- Android Gradle Plugin: 8.6.0; Gradle wrapper: 8.10.
- JDK: the existing OpenJDK 19.0.1 installation. Select the same Gradle JDK in
  Android Studio if opening the generated Gradle project.
- CMake and Ninja from the Felgo Tools installation.

Install the SDK platform, platform-tools, NDK and SDK Build-Tools through
Android Studio's SDK Manager. AGP 8.6 requires Build-Tools 34.0.0 or newer;
allow Gradle to install its required version if only 33.0.1 is configured.
Accept the SDK licenses. The first packaging run needs internet access for
Google Maven, Maven Central and the Felgo Maven repository.

## Build with Qt Creator

1. Open **this checkout's** `CMakeLists.txt`. In the root workspace this is
   `delearner-general/delearner-frontend/CMakeLists.txt`. The older sibling
   `DELerner/delearner` is a separate checkout; fixes here do not update it.
2. Select **Android Qt 6.8.3 Clang arm64-v8a**, configuration **Debug**, and a
   fresh build directory, for example `build/android-arm64-debug`.
3. In Projects > Build > CMake, set `DELEARNER_BUILD_TESTS=OFF` for the app
   package and `QT_USE_TARGET_ANDROID_BUILD_DIR=ON`. Set
   `QT_NO_GLOBAL_APK_TARGET_PART_OF_ALL=ON` so the separate Build Android APK
   step handles packaging. Keep unit tests in a separate desktop build. Verify the SDK, NDK and JDK paths in the Android
   preferences and the kit's host Qt path.
4. Run CMake, build the `all` target (including APK staging), then execute
   **Build Android APK** with platform `android-35`. C++ compilation alone does not produce the APK.
5. The debug APK is under
   `<build>/android-build-delearner/build/outputs/apk/debug/`.
6. Enable USB debugging, connect/authorize an ARM64 device, select it in Qt
   Creator, and Run or Debug. An emulator must also support `arm64-v8a`.

After changing CMake or the Android package files, rerun CMake and package
again. Do not maintain edits in `android-build-delearner`: it is generated.

## Command-line build algorithm (macOS)

Run from the frontend repository. Adjust the installation paths below.
Use a new build directory when switching kits, ABI or source checkout.

```sh
export JAVA_HOME="$HOME/Library/Java/JavaVirtualMachines/openjdk-19.0.1/Contents/Home"
export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
export ANDROID_NDK="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
export PATH="$JAVA_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$PATH"
FELGO_QT="$HOME/Felgo/Felgo"
FELGO_CMAKE="$HOME/Felgo/Tools/CMake/CMake.app/Contents/bin/cmake"
ANDROID_BUILD="$PWD/build/android-arm64-debug"

# 1. Configure native compilation and generate deployment settings.
"$FELGO_CMAKE" -S . -B "$ANDROID_BUILD" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$HOME/Felgo/Tools/Ninja/ninja" \
  -DCMAKE_TOOLCHAIN_FILE="$FELGO_QT/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DQT_HOST_PATH="$FELGO_QT/macos" \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK="$ANDROID_NDK" \
  -DANDROID_ABI=arm64-v8a \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDELEARNER_BUILD_TESTS=OFF \
  -DQT_USE_TARGET_ANDROID_BUILD_DIR=ON \
  -DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL=ON

# 2. Compile C++/QML and stage the native application library.
"$FELGO_CMAKE" --build "$ANDROID_BUILD" --target all --parallel

# 3. Copy Qt/Felgo dependencies and build the debug APK with Gradle.
"$FELGO_QT/macos/bin/androiddeployqt" \
  --input "$ANDROID_BUILD/android-delearner-deployment-settings.json" \
  --output "$ANDROID_BUILD/android-build-delearner" \
  --android-platform android-35 --jdk "$JAVA_HOME" --gradle

# 4. Optional: install on the connected device (use adb -s SERIAL for several).
adb devices
adb install -r "$ANDROID_BUILD/android-build-delearner/build/outputs/apk/debug/android-build-delearner-debug.apk"
```

The explicit `QT_CHAINLOAD_TOOLCHAIN_FILE` avoids the installed kit's embedded
Linux NDK path (`/opt/android/android-ndk-r27c`) being used on macOS.
For subsequent native/QML changes, repeat steps 2–3 and reinstall.

## Fix for missing androidGradleToolsVersion

The error occurs before app compilation in Gradle: `android/build.gradle`
expects Felgo's `androidGradleToolsVersion`, product ID and version properties,
but the failed output contains only Qt's defaults. Felgo generates these
properties during CMake configuration, directly in the deployment directory.
A local `android/gradle.properties` containing only Qt defaults can also
overwrite those generated settings during deployment. The original source
package alone cannot reconstruct Felgo's values.

CMake now stages the Android package in `android-package-delearner`, including
a copy of Felgo's generated properties, and passes that directory to
`androiddeployqt`. Deployment therefore restores the properties along with the
manifest and Gradle script. The generated properties are written last, even when a local default properties
file was copied into the staging directory moments earlier. Versions remain
supplied by the installed kit and
`PRODUCT_*` CMake settings. Rerun CMake to apply this fix to an existing build.

The reported Windows/macOS/iOS Controls import warnings did not cause this
Gradle failure. Investigate QML warnings separately if the packaged app reports
an actual missing import at startup.

## Run and debug with Android Studio

Build the **Debug APK** using the steps above first. Android Studio can debug
the packaged C++ application using LLDB:

1. In Android Studio choose **Profile or Debug APK** (on the welcome screen,
   under More Actions in some versions, or File > Profile or Debug APK).
   Select the generated debug APK.
2. Install the NDK/tools when prompted. In the Project pane's Android view,
   expand `cpp` and open `libdelearner_arm64-v8a.so`.
3. Attach native debug symbols with **Add**, selecting the directory containing
   the unstripped library from the **same build**, normally the CMake build
   directory. Do not select Gradle's stripped/intermediate library. Use
   `find "$ANDROID_BUILD" -name 'libdelearner*.so'` to locate candidates.
4. If sources are not found, use **Path Mappings > Local Paths** to map the
   embedded source path to this frontend checkout, then Apply Changes.
5. Select the connected ARM64 device/emulator. In Run > Edit Configurations,
   choose Native Only or Dual debugging where available. Set a breakpoint in
   an app C++ method, click Debug and trigger that method in the app.
6. Use Logcat with the application ID
   `com.yourcompany.wizardEVAP.delearner` to inspect Qt/app messages.
7. After edits, rebuild and repackage with Qt/CMake. Re-import the updated APK
   when Android Studio prompts; keep symbols and APK from the same build.

Android Studio's native debugger handles C++; use Qt Creator for QML/JavaScript
breakpoints and Qt Quick inspection. Qt/Felgo internals require their own
matching debug symbols for source-level stepping.

Alternatively, open `<build>/android-build-delearner` as an existing Gradle
project, select the matching Gradle JDK, sync and run its application module.
Keep the supplied Gradle/AGP versions when prompted about upgrades. This project
packages the already compiled `.so` files: Android Studio's Build/Run alone does
not rebuild the Qt C++/QML application. Repeat native build and deployment first.

References: [Qt androiddeployqt](https://doc.qt.io/qt-6/android-deploy-qt-tool.html),
[Android Studio APK debugging](https://developer.android.com/studio/debug/apk-debugger),
[AGP 8.6 compatibility](https://developer.android.com/build/releases/past-releases/agp-8-6-0-release-notes).

## Build verification (2026-09-13)

Verified with the installed kit above:

- Reproduced the original missing-property error using Gradle `help --offline`
  on a copy of the failed package.
- Built the frontend C++/QML application for ARM64 Debug and packaged the APK.
- Moved the deployment output's `gradle.properties` aside, reran the step 3
  `androiddeployqt` command without reconfiguring CMake, and obtained
  `BUILD SUCCESSFUL`. The product and AGP properties were restored.
- Confirmed the native application library contains DWARF debug information.

To repeat the regression check, move only
`$ANDROID_BUILD/android-build-delearner/gradle.properties` to a temporary backup,
then repeat step 3. Check that packaging succeeds and the restored properties
include `androidGradleToolsVersion`, `productIdentifier`, `productVersionName`
and `productVersionCode`. Leave `android-package-delearner` intact: it is the
configured source used to restore the deployment output.

Device launch and an Android Studio breakpoint session were not exercised.
