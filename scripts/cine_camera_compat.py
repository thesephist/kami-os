# Link-only option: keep the installed SDK archive untouched. The wrapper only
# changes the SDK camera-task stack during Cine's isolated RAW configuration.
Import('env')
env.Append(LINKFLAGS=['-Wl,--wrap=xTaskCreatePinnedToCore'])
