add_requires('glfw3', {optional = true})

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
    if is_plat('macosx') then
        add_frameworks('OpenGL')
    elseif is_plat('linux') then
        add_links('GL')
    end
    add_headerfiles('bmflat.h')
    add_files('bmflat.c')
    add_files('flatspin.c')
