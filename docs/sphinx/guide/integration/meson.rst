Meson
=====

ACEWS is designed to be easily integrated into a Meson project as a Meson
subproject.

Using it is as simple as placing the project in your toplevel project's
``subprojects`` directory and listing it as a dependency. For example::

    sources = [
        'src/main.c',
    ]

    depends = [
        dependency('acews'),
    ]

    executable('program',
        sources,
        dependencies: depends,
    )

There is also a helper script, ``bin2c.py`` which can be used to convert binary
files to text files. This is helpful for converting public and private key
pairs to embeddable C source code. As an example::

    acews_proj = subproject('acews')
    bin2c_py = acews_proj.get_variable('bin2c_py')

    private_der_c = custom_target(
        input: server_provate_der,
        ouput: 'server-private-der.c',
        command: [bin2c_py, '@INPUT@', '@OUTPUT@'],
    )

    sources += [
        private_der_c,
    ]
