# Pakon F-X35 Custom Drivers

## What is this?

This repository contains the source code for drivers, compatible with modern 64-bit versions of Windows, that I created for the Pakon "F-X35" line of film scanners. The drivers were developed through a multi-week research effort, documented extensively on [my blog](https://ktkaufman03.github.io/blog/2022/09/04/pakon-reverse-engineering/)[^1]

## Why does this exist?

The original Pakon scanner drivers suffered from two major issues:
1. No 64-bit versions were ever made, which automatically prevents the scanners from being used with a Windows 11 system.
2. The 32-bit versions that existed had a critical bug that prevented them from working on systems running Windows Vista or later. As a result, the only "reasonable" option was to use a system running Windows XP, which is far from ideal.

As it turns out, there is a sizable group of people who rely on these scanners - both for personal and professional jobs - so I decided to try my hand at improving the situation.

## What's in this repository?

The project consists of 4 components:

| Folder            | Purpose                                                                                                                   |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------- |
| FX35Loader        | This is the driver that deploys firmware to the scanner upon connection and power-on.                                     |
| FX35Package       | This contains all of the resource files for the different scanner models, such as firmware and Windows driver info files. |
| FX35USB           | This is the actual scanner driver, responsible for relaying data between the scanner and the scanning client.             |
| SkmPakonInstaller | This is the installer for the drivers. It's effectively a wrapper around `pnputil` and some other tedious tasks.          |

[^1]: See also: https://news.ycombinator.com/item?id=32714806
