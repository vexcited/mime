/*
 * the identifiers of the controls in the color, font, print and page setup dialogs. programs write hooks and
 * templates against these, so they are those of dlgs.h.
 */
#ifndef MIME_COMDLG32_DLGIDS_H
#define MIME_COMDLG32_DLGIDS_H

/* the generic ones: push buttons, check boxes, radio buttons, groups, statics, lists, combos, edits. */
#define psh1 0x0400
#define psh2 0x0401
#define psh3 0x0402
#define pshHelp 0x040e
#define chx1 0x0410
#define chx2 0x0411
#define rad1 0x0420
#define rad2 0x0421
#define rad3 0x0422
#define grp1 0x0430
#define grp2 0x0431
#define grp3 0x0432
#define grp4 0x0433
#define stc1 0x0440
#define stc2 0x0441
#define stc3 0x0442
#define stc4 0x0443
#define stc5 0x0444
#define stc6 0x0445
#define stc7 0x0446
#define stc8 0x0447
#define stc9 0x0448
#define stc10 0x0449
#define stc11 0x044a
#define stc12 0x044b
#define lst1 0x0460
#define cmb1 0x0470
#define cmb2 0x0471
#define cmb3 0x0472
#define cmb4 0x0473
#define cmb5 0x0474
#define edt1 0x0480
#define edt2 0x0481
#define edt3 0x0482
#define edt4 0x0483
#define edt5 0x0484
#define edt6 0x0485
#define edt7 0x0486
#define edt8 0x0487
#define ctl1 0x04A0

/* color. */
#define COLOR_HUESCROLL 700
#define COLOR_SATSCROLL 701
#define COLOR_LUMSCROLL 702
#define COLOR_HUE 703
#define COLOR_SAT 704
#define COLOR_LUM 705
#define COLOR_RED 706
#define COLOR_GREEN 707
#define COLOR_BLUE 708
#define COLOR_CURRENT 709
#define COLOR_RAINBOW 710
#define COLOR_SAVE 711
#define COLOR_ADD 712
#define COLOR_SOLID 713
#define COLOR_TUNE 714
#define COLOR_SCHEMES 715
#define COLOR_ELEMENT 716
#define COLOR_SAMPLES 717
#define COLOR_PALETTE 718
#define COLOR_MIX 719
#define COLOR_BOX1 720
#define COLOR_CUSTOM1 721
#define COLOR_HUELABEL 723
#define COLOR_SATLABEL 724
#define COLOR_LUMLABEL 725
#define COLOR_REDLABEL 726
#define COLOR_GREENLABEL 727
#define COLOR_BLUELABEL 728
#define COLOR_COLORLABEL 730
#define COLOR_SOLIDLABEL 731

/* font: the family, style and size lists, the effects, the sample and the script. */
#define FONT_FAMILY cmb1
#define FONT_STYLE cmb2
#define FONT_SIZE cmb3
#define FONT_COLOR cmb4
#define FONT_SCRIPT cmb5
#define FONT_STRIKEOUT chx1
#define FONT_UNDERLINE chx2
#define FONT_SAMPLE stc5
#define FONT_INFO stc6
#define FONT_APPLY psh3

#endif
