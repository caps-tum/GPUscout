x = {
            "memory_flow": {
                "general": {
                    "l2_cache_hit_perc": 86.59,
                    "l2_queries": 2359.0,
                    "loads_l2_cache_hit_perc": 24.87,
                    "loads_l2_to_dram_bytes": 1538.6624,
                    "stores_l2_cache_hit_perc": 100.0,
                    "stores_l2_to_dram_bytes": 0.0
                },
                "global": {
                    "atomic_l1_cache_hit_perc": 0.0,
                    "atomic_to_l1_bytes": 0.0,
                    "atomics_l1_to_l2_bytes": 0.0,
                    "atomics_l2_cache_hit_perc": 0.0,
                    "atomics_l2_to_dram_bytes": 0.0,
                    "bytes_per_instruction": 15.058823529411764,
                    "instructions": 32.0,
                    "loads_instructions": 16.0,
                    "loads_l1_cache_hit_perc": 0.0,
                    "loads_l1_to_l2_bytes": 2048.0,
                    "loads_to_l1_bytes": 2048.0,
                    "stores_instructions": 16.0,
                    "stores_l1_cache_hit_perc": 0.0,
                    "stores_l1_to_l2_bytes": 2048.0,
                    "stores_to_l1_bytes": 2048.0
                },
                "local": {
                    "instructions": 0.0,
                    "l2_queries_perc": 0.0,
                    "loads_instructions": 0.0,
                    "loads_l1_cache_hit_perc": 0.0,
                    "loads_l1_to_l2_bytes": 0.0,
                    "loads_to_l1_bytes": 0.0,
                    "stores_instructions": 0.0,
                    "stores_l1_cache_hit_perc": 0.0,
                    "stores_l1_to_l2_bytes": 0.0,
                    "stores_to_l1_bytes": 0.0
                },
                "shared": {
                    "instructions": 0.0,
                    "ldgsts_instructions": 0.0,
                    "loads_bank_conflict": 0,
                    "loads_efficiency_perc": 0.0,
                    "loads_instructions": 0.0,
                    "stores_instructions": 0.0
                },
                "surface": {
                    "instructions": 0.0,
                    "loads_instructions": 0.0,
                    "loads_l1_cache_hit_perc": 0.0,
                    "loads_l1_to_l2_bytes": 0.0,
                    "loads_to_l1_bytes": 0.0,
                    "stores_instructions": 0.0,
                    "stores_l1_cache_hit_perc": 0.0,
                    "stores_l1_to_l2_bytes": 0.0,
                    "stores_to_l1_bytes": 0.0
                },
                "texture": {
                    "instructions": 0.0,
                    "loads_l1_cache_hit_perc": 0.0,
                    "loads_l1_to_l2_bytes": 0.0,
                    "loads_l2_to_dram_bytes": 0.0,
                    "loads_to_l1_bytes": 0.0
                }
            },
}

def get_all_keys(d, u):
    for key, value in d.items():
        yield u + "." + key
        if isinstance(value, dict):
            yield from get_all_keys(value, u + "." + key)

for key in get_all_keys(x, ""):
    s = """TITLE: {
        name: 'NAME',
        display_name: 'DISPLAY',
        hint: '',
        format_function: FORMAT,
        help_text: 'This is a detailed explanation something',
        lower_better: true
    },"""
    s = s.replace('TITLE', key[1:].replace('.', '_').replace('memory_flow_', '')).replace('NAME', key[1:].replace('.', '/')).replace('DISPLAY', key[1:].replace('.', ' '))
    s = s.replace('FORMAT', 'formatBytes' if 'bytes' in key else ('formatPercent' if 'perc' in key else 'formatInstructions'))
    print(s)
