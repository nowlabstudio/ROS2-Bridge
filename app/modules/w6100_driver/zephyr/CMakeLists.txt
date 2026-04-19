zephyr_library_named(eth_w6100)

zephyr_library_sources_ifdef(CONFIG_ETH_W6100
	${CMAKE_CURRENT_LIST_DIR}/../drivers/ethernet/eth_w6100.c
)

zephyr_library_include_directories_ifdef(CONFIG_ETH_W6100
	${CMAKE_CURRENT_LIST_DIR}/../drivers/ethernet
	${ZEPHYR_BASE}/drivers/ethernet
)
