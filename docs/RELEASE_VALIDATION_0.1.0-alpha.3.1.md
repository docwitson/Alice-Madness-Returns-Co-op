# Alpha.3.1 display hotfix validation

Checks completed on 2026-10-17:

- Fresh source snapshot of the final candidate (ultrawide-test7): Debug and
  Release x86 builds; unit tests, relay self-test and launcher self-test passed
  in both configurations. Source code for the final release matches that
  manually tested candidate; release documentation is updated separately.
- ProcessEvent bridge layout and native Scaleform action/navigation checks passed.
- Release installer and drop-in passed exact contents, manifests, checksums,
  quiet defaults, isolated install/repair/uninstall and packaged relay probing.
  Package lint reported 43 files, zero failures and zero warnings.
- Microsoft Defender custom scan of the final local candidate reported no
  matching detections. This result applies to that candidate's binary hashes;
  final CI artifacts have their own checksums. No VirusTotal upload is performed
  by the release process.
- Initial launch tests covered direct launch and Steam `-applaunch 19680`,
  including legacy positive INI dimensions. Windowed size and position stayed
  stable across repeated observations. The WPF display-mode selection event
  saved immediately without launching a game and retained the choice on close.
- Subsequent user manual tests covered native 3440x1440 and 1920x1080, startup
  and resolution changes, and Windowed, Borderless and Fullscreen modes. Earlier
  candidates exposed independent GFx, scene-family and video-renderer aspect
  issues. The final candidate corrected those paths; the user confirmed the
  remaining fullscreen pause/video cases were working after manual checks.

The aspect hooks target the verified current Steam/EA App executable layout.
Legacy executable address entries are zero, retaining their native aspect
behavior. Scene-family and video hooks verify native instruction bytes before
installing. Source movie dimensions and normal letterbox fitting are preserved.

These checks cover display behavior, menus, startup and package lifecycle, not a
new full campaign playthrough. Earlier Alpha.3 gameplay/audio testing and its
known limitations still apply. See the
[Alpha.3 validation record](RELEASE_VALIDATION_0.1.0-alpha.3.md).

Protocol 43 and gameplay/network synchronization are unchanged. Update the DLL
and launcher using Install/Repair; opening a new launcher alone does not replace
an installed DLL. The original published Alpha.3 archives and tag are preserved.
