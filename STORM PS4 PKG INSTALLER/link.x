ENTRY(_start_ps4_c)

SECTIONS
{
	.text : ALIGN(0x4000) {
		__text_start = .;
		QUAD(0x6365786562696C2F);
		QUAD(0x2E666C652D646C2F);
		QUAD(0x00000000312E6F73);
		QUAD(0x0000000000000000);
		*(.text .text.*)
	}
	.rodata : ALIGN(0x10) { *(.rodata .rodata.*) }
	.eh_frame : { __eh_frame_start = .; KEEP(*(.eh_frame)); __eh_frame_end = .; }
	.eh_frame_hdr : { __eh_frame_hdr_start = .; KEEP(*(.eh_frame_hdr)); __eh_frame_hdr_end = .; }
	.data.rel.ro : ALIGN(0x4000) { __data_relro_start = .; KEEP(*(.data.rel.ro .data.rel.ro.*)); }
	.init_array : { *(.init_array); }
	.dynamic : { *(.dynamic); }
	.tls : { *(.tdata); *(.tbss); }
	.got : ALIGN(SIZEOF(.data.rel.ro) > 0 ? 8 : 0x4000) { *(.got) }
	.got.plt : ALIGN((SIZEOF(.got) > 0 || SIZEOF(.data.rel.ro) > 0) ? 8 : 0x4000) { *(.got.plt) }
	.data.sce_process_param : ALIGN(0x4000) { KEEP(*(.data.sce_process_param)) }
	.data.sce_module_param : ALIGN(0x4000) { KEEP(*(.data.sce_module_param)) }
	.data : { *(.data) }
	.bss : { *(.bss .bss.*) }
	/DISCARD/ : { QUAD(_GLOBAL_OFFSET_TABLE_) }
}
