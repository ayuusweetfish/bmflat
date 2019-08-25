package('glfw3')
    set_urls('https://github.com/glfw/glfw/releases/download/$(version)/glfw-$(version).zip')
    add_versions('3.3', '36fda4cb173e3eb2928c976b0e9b5014e2e5d12b9b787efa0aa29ffc41c37c4a')
    add_deps('cmake')
    on_install(function (package)
        import('package.tools.cmake').install(package)
    end)

add_requires('glfw3', {optional = true})
add_requires('glew', {optional = true})

rule('bms')
    set_extensions('.bms', '.bme', '.bml')
    on_build_file(function (target, source)
        os.cp(source, path.join(target:targetdir(), source))
    end)

target('bmflat')
    set_kind('binary')
    add_rules('bms')
    add_headerfiles('bmflat.h')
    add_files('bmflat.c')
    add_files('main.c')
    add_files('sample.bms')

target('bmflatspin')
    set_kind('binary')
    add_packages('glfw3')
    add_packages('glew')
    if is_plat('macosx') then
        add_frameworks('OpenGL')
    elseif is_plat('linux') then
        add_links('GL')
    elseif is_plat('windows') then
        add_links('user32', 'ole32', 'comdlg32', 'shell32', 'gdi32', 'opengl32')
    end
    add_headerfiles('bmflat.h')
    add_files('bmflat.c')
    add_files('flatspin.c')
    add_files('miniaudio/extras/stb_vorbis.c')
    add_files('tinyfiledialogs/tinyfiledialogs.c')
