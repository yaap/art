# Ahat Demo App

This is a simple Android application designed to demonstrate an activity leak for testing `ahat`.

## How to trigger the leak
1. Launch the app.
2. Rotate the screen.
3. Take a heap dump (`adb shell am dumpheap com.android.ahat.demo /data/local/tmp/dump.hprof`).
4. Pull the heap dump (`adb pull /data/local/tmp/dump.hprof`).
5. Open the heap dump in `ahat`.
6. Go to the "Activity Leaks" page.
