## Installation and Usage

1. **Navigate to the plugin directory**  
   Open a terminal and run:
   cd /path/to/plugin-directory

2. **Build the plugin**  
   Use `make` to compile the plugin:
   make

3. **Install the plugin**  
   Copy the compiled `.so` file to your LADSPA plugin directory:
   cp *.so ~/.ladspa/
   Alternatively, you can copy it to a custom directory recognized by your DAW or plugin host.

4. **Use the plugin**  
   Load the plugin in any LADSPA-compatible digital audio workstation (DAW) such as:
   - Ardour  
   - Qtractor  
   - LMMS (with LADSPA support)  
   - Carla (plugin rack host)

## Notes

- Ensure your DAW or plugin host is configured to scan the directory where you placed the `.so` file.
- On some systems, you may need to create the `~/.ladspa/` directory manually.

---

Enjoy making music!
