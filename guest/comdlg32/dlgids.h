/*
 * control ids of the color, font, print and page setup dialogs. program hooks and templates rely on them, so they
 * match dlgs.h.
 */
#ifndef MIME_COMDLG32_DLGIDS_H
#define MIME_COMDLG32_DLGIDS_H

/* generic ids: push buttons, check boxes, radio buttons, groups, statics, lists, combos, edits. */
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


/* print. */
#define PRN_GROUP_PRINTER 1075
#define PRN_NAME_LABEL 1093
#define PRN_NAME 1139
#define PRN_PROPERTIES 1025
#define PRN_STATUS_LABEL 1095
#define PRN_STATUS 1099
#define PRN_TYPE_LABEL 1094
#define PRN_TYPE 1098
#define PRN_WHERE_LABEL 1097
#define PRN_WHERE 1101
#define PRN_COMMENT_LABEL 1096
#define PRN_COMMENT 1100
#define PRN_TOFILE 1040
#define PRN_GROUP_RANGE 1072
#define PRN_ALL 1056
#define PRN_SELECTION 1057
#define PRN_PAGES 1058
#define PRN_EX_CURRENT 1058
#define PRN_EX_PAGES 1059
#define PRN_FROM_LABEL 1089
#define PRN_FROM 1152
#define PRN_TO_LABEL 1090
#define PRN_TO 1153
#define PRN_RANGES_HINT 1088
#define PRN_GROUP_COPIES 1073
#define PRN_COPIES_LABEL 1092
#define PRN_COPIES 1154
#define PRN_COLLATE_ICON 1086
#define PRN_COLLATE 1041
#define PRN_APPLY 1026

/* page setup. */
#define PSD_PAGE 1080
#define PSD_RIGHT_SHADOW 1081
#define PSD_BOTTOM_SHADOW 1082
#define PSD_GROUP_PAPER 1073
#define PSD_SIZE_LABEL 1089
#define PSD_SIZE 1137
#define PSD_SOURCE_LABEL 1090
#define PSD_SOURCE 1138
#define PSD_GROUP_ORIENTATION 1072
#define PSD_PORTRAIT 1056
#define PSD_LANDSCAPE 1057
#define PSD_GROUP_MARGINS 1075
#define PSD_LEFT_LABEL 1102
#define PSD_LEFT 1155
#define PSD_RIGHT_LABEL 1103
#define PSD_RIGHT 1157
#define PSD_TOP_LABEL 1104
#define PSD_TOP 1156
#define PSD_BOTTOM_LABEL 1105
#define PSD_BOTTOM 1158
#define PSD_PRINTER 1026

#endif
