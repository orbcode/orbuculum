project('orbuculum', 'c')

dependencies = [
    dependency('threads'),
    dependency('libusb-1.0'),
    dependency('libzmq'),
    dependency('SDL2'),
    dependency('ncurses', 'ncursesw')
]

incdirs = include_directories(['Inc', 'Inc/external'])
cc = meson.get_compiler('c')

if host_machine.system() == 'windows'
    winsock2 = cc.find_library('ws2_32')
    dependencies += [
        winsock2
    ]
elif host_machine.system() == 'darwin'
    add_global_arguments('-DOSX', language: 'c')
else
    add_global_arguments('-DLINUX', language: 'c')
    add_global_arguments('-D_GNU_SOURCE', language: 'c')
    install_data('Support/60-orbcode.rules', install_dir : '/etc/udev/rules.d')
    install_data('Support/gdbtrace.init', install_dir : 'share/orbcode')
endif

add_global_arguments('-DSCREEN_HANDLING', language: 'c')
add_global_arguments(['-include', 'uicolours_default.h'], language: 'c')

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
        'Src/traceDecoder.c',
        'Src/generics.c',
    ] + stream_src,
    include_directories: incdirs,
    dependencies: dependencies,
    soversion: 0,
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
        'Src/symbols.c',
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
