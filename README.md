# shell Binding

shell binding support shell exec with format conversion for output ...

Checkout the documentation from sources on docs folder or on [the redpesk documentation](http://docs.redpesk.bzh/docs/en/master/apis-services/shellexec/shell_binding_doc.html).




Fulup TDL.

- finir le parsing de la config avec les valeur par default formating, buffer, ...
- faire la recherche de formatage
- traiter la fermeture du pipe dans la callback systemd
-
- namespace:
    - montage mount
    - visibilité réseau
    -
- uid/gid
    - si CAP_IS_SUPPORTED(CAP_SETUID/CAP_SETGID) setuid / getgid obligatoire
    - sinon warning pour dire que pas pris en compte.
    -
- create cgroups
    - mkdir /run/user/1000/cgroup
    - mount -t cgroup2 none /run/user/1000/cgroup

reference:
    https://doc.opensuse.org/documentation/leap/tuning/book-sle-tuning_color_en.pdf
    https://www.youtube.com/watch?v=ikZ8_mRotT4
    https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html

https://blog.container-solutions.com/linux-capabilities-why-they-exist-and-how-they-work

seccomp
    https://blog.yadutaf.fr/2014/05/29/introduction-to-seccomp-bpf-linux-syscall-filter/

    add to /etc/default/grub =>  systemd.unified_cgroup_hierarchy=1 cgroup=no_v1=all
    grub2-mkconfig -o /boot/grub2/grub.cfg
    reboot
    cat /proc/cmdline
    mount | grep cgroup2






* Pour path de fichier tester le 1er char et si alphanum ajouter httproot

* sortir l'init encodeur de la callback generic
* Mettre une table de callback utilities for les encodeurs
* coder l'encodeur json
* coder l'encodeur ligne
* coder un encodeur 10l/10l comme demo

1) voir pour le verb info
2) Ajouter bwrap
3) faire la doc