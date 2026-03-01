# filter_config.py - Post-process generated config to remove unwanted categories
# Runs AFTER extra_script.py prebuild() has merged all config.json files
# Add to platformio.ini: extra_scripts = ${env.extra_scripts}
#                                         pre:lib/halmettask/filter_config.py

import os
import json
import gzip
import inspect

Import("env")

def basePath():
    # Same approach as extra_script.py - works in SCons context
    return os.path.dirname(os.path.dirname(os.path.dirname(inspect.getfile(lambda: None))))

def get_filter_categories(env):
    """Get list of category patterns to filter out from custom options"""
    # Default categories to remove (can be overridden in platformio.ini)
    # custom_filter_categories = iicsensors, spisensors, xdr
    try:
        opt = env.GetProjectOption("custom_filter_categories")
        if opt:
            return [c.strip() for c in opt.replace("\n", ",").split(",") if c.strip()]
    except:
        pass
    return []

def get_preserve_constants(env):
    """Get list of config constant names to preserve even when category is filtered"""
    # custom_preserve_constants = talkerId, sendN2k
    # Or from halmet_filters section: preserve_constants
    try:
        opt = env.GetProjectOption("custom_preserve_constants")
        if opt:
            return [c.strip() for c in opt.replace("\n", ",").split(",") if c.strip()]
    except:
        pass
    # Try reading from interpolated value
    try:
        opt = env.GetProjectOption("custom_filter_preserve")
        if opt:
            return [c.strip() for c in opt.replace("\n", ",").split(",") if c.strip()]
    except:
        pass
    return []

def should_filter_item(item, filter_patterns):
    """Check if a config item should be filtered based on its category"""
    category = item.get('category', '')
    if not category:
        return False
    for pattern in filter_patterns:
        # Support wildcards: "xdr" matches "xdr1", "xdr2", etc.
        if pattern.endswith('*'):
            if category.startswith(pattern[:-1]):
                return True
        elif pattern in category or category.startswith(pattern):
            return True
    return False

def filter_config(env):
    """Filter the generated config.json to remove unwanted categories"""
    filter_patterns = get_filter_categories(env)
    if not filter_patterns:
        print("[filter_config] No filter patterns configured (custom_filter_categories)")
        return
    
    print(f"[filter_config] Filtering categories: {filter_patterns}")
    
    # Paths - use inspect-based basePath() like extra_script.py
    base_path = basePath()
    gen_path = os.path.join(base_path, 'lib', 'generated')
    config_json = os.path.join(gen_path, 'config.json')
    config_gz = config_json + '.gz'
    cfg_header = os.path.join(gen_path, 'GwConfigDefinitions.h')
    cfg_impl = os.path.join(gen_path, 'GwConfigDefImpl.h')
    
    if not os.path.exists(config_json):
        print(f"[filter_config] WARNING: {config_json} not found, skipping")
        return
    
    # Load the merged config
    with open(config_json, 'r') as f:
        config = json.load(f)
    
    original_count = len(config)
    
    # Filter out unwanted items
    filtered_config = [item for item in config if not should_filter_item(item, filter_patterns)]
    
    filtered_count = original_count - len(filtered_config)
    print(f"[filter_config] Removed {filtered_count} of {original_count} config items")
    
    if filtered_count == 0:
        return
    
    # Write filtered config back
    with open(config_json, 'w') as f:
        json.dump(filtered_config, f, indent=2)
    
    # Regenerate compressed version
    print(f"[filter_config] Regenerating {config_gz}")
    with open(config_json, 'rb') as f_in:
        with gzip.open(config_gz, 'wb') as f_out:
            f_out.write(f_in.read())
    
    # Regenerate header files
    preserve_constants = get_preserve_constants(env)
    regenerate_headers(filtered_config, cfg_header, cfg_impl, config_json, preserve_constants)

def regenerate_headers(config, header_file, impl_file, source_file, preserve_constants=None):
    """Regenerate the C++ header files from filtered config"""
    if preserve_constants is None:
        preserve_constants = []
    
    # Collect names that are in the filtered config
    config_names = set(item.get('name') for item in config if item.get('name'))
    
    # Generate GwConfigDefinitions.h
    header_data = f"//generated from {source_file} (filtered)\n"
    header_data += '#include "GwConfigItem.h"\n'
    header_data += 'class GwConfigDefinitions{\n'
    header_data += '  public:\n'
    header_data += f'  int getNumConfig() const{{return {len(config)};}}\n'
    
    for item in config:
        name = item.get('name')
        if name is None:
            continue
        if len(name) > 15:
            raise Exception(f"{name}: config names must be max 15 characters")
        header_data += f'  static constexpr const char* {name}="{name}";\n'
    
    # Add preserved constants that weren't in the filtered config
    preserved_added = []
    for const_name in preserve_constants:
        if const_name not in config_names:
            header_data += f'  static constexpr const char* {const_name}="{const_name}"; // preserved stub\n'
            preserved_added.append(const_name)
    
    if preserved_added:
        print(f"[filter_config] Preserved constants: {preserved_added}")
    
    header_data += "};\n"
    
    print(f"[filter_config] Regenerating {header_file}")
    with open(header_file, 'w') as f:
        f.write(header_data)
    
    # Generate GwConfigDefImpl.h
    impl_data = f"//generated from {source_file} (filtered)\n"
    impl_data += 'void GwConfigHandler::populateConfigs(GwConfigInterface **config){\n'
    
    idx = 0
    for item in config:
        name = item.get('name')
        if name is None:
            continue
        secret = "true" if item.get('type') == 'password' else "false"
        default = item.get('default', '')
        impl_data += f'  configs[{idx}]=     new GwConfigInterface({name},"{default}",{secret});\n'
        idx += 1
    
    impl_data += '}\n'
    
    print(f"[filter_config] Regenerating {impl_file}")
    with open(impl_file, 'w') as f:
        f.write(impl_data)

# Run the config filter
filter_config(env)
