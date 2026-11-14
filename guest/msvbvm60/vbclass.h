/*
 * the intrinsic controls of Visual Basic 6: for each class its properties in the order the runtime keeps them, the events of its
 * sink and the layout of the vtable the runtime gives the object of a control. vbclass.c is made from
 * the runtime of Windows.
 */
#ifndef VBCLASS_H
#define VBCLASS_H

typedef struct {
    const char *name;
    unsigned char type;
    unsigned long deflt;
} VBPROP;

/* what the compiled code reaches through a slot of the vtable: 1 reads a property, 2 writes it, 3 and 4 call a method. */
typedef struct {
    unsigned char kind;
    unsigned char index;
    unsigned char ret;
    unsigned char type;
} VBSLOT;

typedef struct {
    const char *name;
    const VBPROP *props;
    int nprops;
    const char *const *events;
    int nevents;
    const VBSLOT *slots;
    int nslots;
} VBCLASS;

extern const VBCLASS g_classes[];
extern const int g_class_count;

enum {
    VBC_PICTUREBOX = 0, VBC_LABEL = 1, VBC_TEXTBOX = 2, VBC_FRAME = 3, VBC_COMMAND = 4, VBC_CHECKBOX = 5, VBC_OPTION = 6,
    VBC_COMBOBOX = 7, VBC_LISTBOX = 8, VBC_HSCROLL = 9, VBC_VSCROLL = 10, VBC_TIMER = 11, VBC_PRINTER = 12, VBC_SCREEN = 14, VBC_CLIPBOARD = 15, VBC_FORM = 13, VBC_MENU = 19, VBC_APP = 21
};

#endif
