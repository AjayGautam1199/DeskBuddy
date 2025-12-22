SET(SOURCES screens/ui_Screen1.c
    screens/ui_Screen2.c
    screens/ui_Screen3.c
    ui.c
    ui_helpers.c
    images/ui_img_ready_png.c
    images/ui_img_mega_png.c)

add_library(ui ${SOURCES})
