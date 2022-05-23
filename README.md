
# ZenPower    [![Badge License]][License]

*Linux kernel Driver AMD Zen Family CPUs*

<br>

<div align = center>

---

[![Button Install]][Install]   
[![Button Activate]][Activate]   
[![Button Update]][Update]

---

</div>

<br>

## Readings

- ***Temperature***
- ***Voltage*** ( SVI2 )
- ***Current*** ( SVI2 )
- ***Power*** ( SVI2 )

<br>
<br>

## Requirements

- Your **Linux Kernel** must support your **CPU**.

    ***ZenPower*** *reads values from **SMN** with* <br>
    *the kernel function:* `amd_smn_read`
    
    *There is a fallback, however* <br>
    *it is not guaranteed to work.*

- `AMD Ryzen 3000` CPUs

    Family: `17h` <br>
    Model:  `70h`

    Require a `5.3.4+` kernel or a **[Patched]** one.


<br>
<br>

## Monitoring

You can use the **[ZenMonitor]** or your <br>
favorite sensor monitoring software.

<br>
<br>

## Help Wanted

It is greatly appreciated if you **[Share Debug Data]** <br>
of **ZenPower** to help future development efforts.

<br>
<br>

## Notes

 - Some users have reported that a restart <br>
   was necessary after module installation.

 - If you are having trouble compiling **ZenPower** under <br>
   `Ubuntu 18.04+` with new upstream kernel, see [#23].
 
 - The meaning of raw current values from **SVI2** <br>
   telemetry are not standardized so the current <br>
   and power readings may not be accurate on <br>
   all systems (depends on the board model).



<!----------------------------------------------------------------------------->

[Badge License]: https://img.shields.io/badge/License-GPL_2-blue.svg?style=for-the-badge

[Activate]: Documentation/Activate.md
[Install]: Documentation/Install.md
[License]: LICENSE
[Update]: Documentation/Update.md

[Share Debug Data]: https://github.com/ocerman/zenpower/issues/12
[ZenMonitor]: https://github.com/ocerman/zenmonitor
[Patched]: https://patchwork.kernel.org/patch/11043277/
[#23]: https://github.com/ocerman/zenpower/issues/23


<!----------------------------------------------------------------------------->

[Button Activate]: https://img.shields.io/badge/Activate-cdad0d?style=for-the-badge
[Button Install]: https://img.shields.io/badge/Install-df6195?style=for-the-badge
[Button Update]: https://img.shields.io/badge/Update-2ba4ab?style=for-the-badge
