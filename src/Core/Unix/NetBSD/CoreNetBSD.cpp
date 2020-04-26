/*
 Derived from source code of TrueCrypt 7.1a, which is
 Copyright (c) 2008-2012 TrueCrypt Developers Association and which is governed
 by the TrueCrypt License 3.0.

 Modifications and additions to the original source code (contained in this file)
 and all other portions of this file are Copyright (c) 2013-2017 IDRIX
 and are governed by the Apache License 2.0 the full text of which is
 contained in the file License.txt included in VeraCrypt binary and source
 code distribution packages.
*/

#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include "CoreNetBSD.h"
#include "Core/Unix/CoreServiceProxy.h"

namespace VeraCrypt
{
	CoreNetBSD::CoreNetBSD ()
	{
	}

	CoreNetBSD::~CoreNetBSD ()
	{
	}

	DevicePath CoreNetBSD::AttachFileToLoopDevice (const FilePath &filePath, bool readOnly) const
	{
		list <string> args;

		if (readOnly)
		{
			args.push_back ("-r");
		}

		args.push_back ("-c");
		args.push_back ("vnd0");
		args.push_back (filePath);

		string dev = StringConverter::Trim (Process::Execute ("vnconfig", args));

		return "/dev/vnd0";
	}

	void CoreNetBSD::DetachLoopDevice (const DevicePath &devicePath) const
	{
		list <string> args;
		args.push_back ("-u");
		args.push_back (devicePath);

		for (int t = 0; true; t++)
		{
			try
			{
				Process::Execute ("vnconfig", args);
				break;
			}
			catch (ExecutedProcessFailed&)
			{
				if (t > 5)
					throw;
				Thread::Sleep (200);
			}
		}
	}

	HostDeviceList CoreNetBSD::GetHostDevices (bool pathListOnly) const
	{
		HostDeviceList devices;
#ifdef TC_MACOSX
		const string busType = "rdisk";
#else
		foreach (const string &busType, StringConverter::Split ("ad da"))
#endif
		{
			for (int devNumber = 0; devNumber < 64; devNumber++)
			{
				stringstream devPath;
				devPath << "/dev/" << busType << devNumber;

				if (FilesystemPath (devPath.str()).IsBlockDevice() || FilesystemPath (devPath.str()).IsCharacterDevice())
				{
					make_shared_auto (HostDevice, device);
					device->Path = devPath.str();
					if (!pathListOnly)
					{
						try
						{
							device->Size = GetDeviceSize (device->Path);
						}
						catch (...)
						{
							device->Size = 0;
						}
						device->MountPoint = GetDeviceMountPoint (device->Path);
						device->SystemNumber = 0;
					}
					devices.push_back (device);

					for (int partNumber = 1; partNumber < 32; partNumber++)
					{
#ifdef TC_MACOSX
						const string partLetter = "";
#else
						foreach (const string &partLetter, StringConverter::Split (",a,b,c,d,e,f,g,h", ",", true))
#endif
						{
							stringstream partPath;
							partPath << devPath.str() << "s" << partNumber << partLetter;

							if (FilesystemPath (partPath.str()).IsBlockDevice() || FilesystemPath (partPath.str()).IsCharacterDevice())
							{
								make_shared_auto (HostDevice, partition);
								partition->Path = partPath.str();
								if (!pathListOnly)
								{
									try
									{
										partition->Size = GetDeviceSize (partition->Path);
									}
									catch (...)
									{
										partition->Size = 0;
									}
									partition->MountPoint = GetDeviceMountPoint (partition->Path);
									partition->SystemNumber = 0;
								}

								device->Partitions.push_back (partition);
							}
						}
					}
				}
			}
		}

		return devices;
	}

	MountedFilesystemList CoreNetBSD::GetMountedFilesystems (const DevicePath &devicePath, const DirectoryPath &mountPoint) const
	{

		static Mutex mutex;
		ScopeLock sl (mutex);

		struct statvfs *sysMountList;
		int count = getmntinfo (&sysMountList, MNT_NOWAIT);
		throw_sys_if (count == 0);

		MountedFilesystemList mountedFilesystems;

		for (int i = 0; i < count; i++)
		{
			make_shared_auto (MountedFilesystem, mf);

			if (sysMountList[i].f_mntfromname[0])
				mf->Device = DevicePath (sysMountList[i].f_mntfromname);
			else
				continue;

			if (sysMountList[i].f_mntonname[0])
				mf->MountPoint = DirectoryPath (sysMountList[i].f_mntonname);

			mf->Type = sysMountList[i].f_fstypename;

			if ((devicePath.IsEmpty() || devicePath == mf->Device) && (mountPoint.IsEmpty() || mountPoint == mf->MountPoint))
				mountedFilesystems.push_back (mf);
		}

		return mountedFilesystems;
	}

	void CoreNetBSD::MountFilesystem (const DevicePath &devicePath, const DirectoryPath &mountPoint, const string &filesystemType, bool readOnly, const string &systemMountOptions) const
	{
		try
		{
			// Try to mount FAT by default as mount is unable to probe filesystem type on BSD
			CoreUnix::MountFilesystem (devicePath, mountPoint, filesystemType.empty() ? "msdos" : filesystemType, readOnly, systemMountOptions);
		}
		catch (ExecutedProcessFailed&)
		{
			if (!filesystemType.empty())
				throw;

			CoreUnix::MountFilesystem (devicePath, mountPoint, filesystemType, readOnly, systemMountOptions);
		}
	}

#ifdef TC_NETBSD
	auto_ptr <CoreBase> Core (new CoreServiceProxy <CoreNetBSD>);
	auto_ptr <CoreBase> CoreDirect (new CoreNetBSD);
#endif
}
