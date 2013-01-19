assert(getprop("ro.product.device") == "SC-02E" || getprop("ro.build.product") == "SC-02E" || 
       getprop("ro.product.device") == "GT-N7105" || getprop("ro.build.product") == "GT-N7105");
ui_print("");
ui_print("");
ui_print("------------------------------------------------");
ui_print("@VERSION");
ui_print("  KBC Developers:");
ui_print("    Homura Akemi");
ui_print("    Ma34s3");
ui_print("    Sakuramilk");
ui_print("------------------------------------------------");
ui_print("");
show_progress(0.500000, 0);

ui_print("flashing kernel image...");
assert(package_extract_file("boot.img", "/tmp/boot.img"),
       write_raw_image("/tmp/boot.img", "/dev/block/mmcblk0pXX"),
       delete("/tmp/boot.img"));
show_progress(0.100000, 0);

ui_print("flash complete. Enjoy!");
set_progress(1.000000);

