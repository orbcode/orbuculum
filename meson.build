
project('orbuculum', 'c', version:'2.1.0', meson_version: '>=0.63')

uicolours_default = declare_dependency(compile_args: ['-include', 'uicolours_default.h'])

cc = meson.get_compiler('c')

sockets = declare_dependency(
    dependencies: host_machine.system() == 'windows' ? [cc.find_library('ws2_32')] : []
)

dependencies = [
    dependency('threads'),
    dependency('libusb-1.0'),
    dependency('libzmq'),
    dependency('SDL2'),
    dependency('ncurses', 'ncursesw'),
    dependency('capstone'),
    dependency('libelf'),
    uicolours_default,
    sockets,
]

incdirs = include_directories(['Inc', 'Inc/external'])

if host_machine.system() == 'windows'
elif host_machine.system() == 'darwin'
    add_project_arguments('-DOSX', language: 'c')
else
    add_project_arguments('-DLINUX', language: 'c')
    add_project_arguments('-ggdb', language: 'c')
    add_project_arguments('-D_GNU_SOURCE', language: 'c')

    install_data('Support/60-orbcode.rules', install_dir : '/etc/udev/rules.d')
    install_data('Support/gdbtrace.init', install_dir : 'share/orbcode')
endif

add_project_arguments('-DSCREEN_HANDLING', language: 'c')
add_project_arguments('-Wno-error=deprecated-declarations', language: 'c')
add_project_arguments(['-include', 'uicolours_default.h'], language: 'c')


libdwarf = subproject('libdwarf').get_variable('libdwarf')
dependencies += libdwarf

if host_machine.system() == 'windows'
    stream_src = [
        'Src/stream_win32.c',
        'Src/stream_file_win32.c',
        'Src/stream_socket_win32.c',
    ]
else
    stream_src = [
        'Src/stream_file_posix.c',
        'Src/stream_socket_posix.c',
    ]
endif

if host_machine.system() == 'windows'
    subdir('win32')
endif

git_version_info_h = vcs_tag(
    command: ['git', 'describe', '--tags', '--always', '--dirty'],
    input: 'Inc/git_version_info.h.in',
    output: 'git_version_info.h',
)

liborb = library('orb',
    sources: [
        'Src/itmDecoder.c',
        'Src/tpiuDecoder.c',
        'Src/msgDecoder.c',
        'Src/msgSeq.c',
	'Src/traceDecoder_etm35.c',
	'Src/traceDecoder_etm4.c',	
	'Src/traceDecoder_mtb.c',
        'Src/traceDecoder.c',
        'Src/generics.c',
	'Src/readsource.c'
    ] + stream_src,
    include_directories: incdirs,
    dependencies: sockets,
    soversion: meson.project_version(),
    install: true,
)

executable('orbuculum',
    sources: [
        'Src/orbuculum.c',
        'Src/nwclient.c',
        'Src/orbtraceIf.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

if host_machine.system() != 'windows'
    executable('orbfifo',
        sources: [
            'Src/orbfifo.c',
            'Src/filewriter.c',
            'Src/itmfifos.c',
            git_version_info_h,
        ],
        include_directories: incdirs,
        dependencies: dependencies,
        link_with: liborb,
        install: true,
    )
endif

executable('orbcat',
    sources: [
        'Src/orbcat.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbtop',
    sources: [
        'Src/orbtop.c',
        'Src/symbols.c',
        'Src/external/cJSON.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbdump',
    sources: [
        'Src/orbdump.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbstat',
    sources: [
        'Src/orbstat.c',
        'Src/symbols.c',
        'Src/ext_fileformats.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbmortem',
    sources: [
        'Src/orbmortem.c',
        'Src/loadelf.c',
        'Src/sio.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbprofile',
    sources: [
        'Src/orbprofile.c',
        'Src/symbols.c',
        'Src/ext_fileformats.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbtrace',
    sources: [
        'Src/orbtrace.c',
        'Src/orbtraceIf.c',
        'Src/symbols.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orbzmq',
    sources: [
        'Src/orbzmq.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)

executable('orblcd',
    sources: [
        'Src/orblcd.c',
        git_version_info_h,
    ],
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: liborb,
    install: true,
)
