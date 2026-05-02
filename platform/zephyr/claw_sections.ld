/*
 * Custom linker sections for rt-claw driver/service/tool registration.
 * Injected via zephyr_linker_sources(SECTIONS).
 *
 * Creates boundary symbols matching the __start_/__stop_ convention
 * used by include/claw/core/class.h.
 */

	SECTION_DATA_PROLOGUE(claw_services,,)
	{
		__start_claw_services = .;
		KEEP(*(SORT(claw_services*)))
		__stop_claw_services = .;
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

	SECTION_DATA_PROLOGUE(claw_drivers,,)
	{
		__start_claw_drivers = .;
		KEEP(*(SORT(claw_drivers*)))
		__stop_claw_drivers = .;
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

	SECTION_DATA_PROLOGUE(claw_tools,,)
	{
		__start_claw_tools = .;
		KEEP(*(SORT(claw_tools*)))
		__stop_claw_tools = .;
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)
