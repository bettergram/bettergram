/*
This file is part of Bettergram.

For license and copyright information please follow this link:
https://github.com/bettergram/bettergram/blob/master/LEGAL
*/
#pragma once

namespace Core {

class Launcher {
public:
	Launcher(
		int argc,
		char *argv[],
		const QString &deviceModel,
		const QString &systemVersion);

	static std::unique_ptr<Launcher> Create(int argc, char *argv[]);

	int exec();

	QString argumentsString() const;
	bool customWorkingDir() const;

	QString deviceModel() const;
	QString systemVersion() const;
	uint64 installationTag() const;

	bool checkPortableVersionFolder();
	void workingFolderReady();
	void writeDebugModeSetting();
	void writeInstallBetaVersionsSetting();

	virtual ~Launcher() = default;

protected:
	enum class UpdaterLaunch {
		PerformUpdate,
		JustRelaunch,
	};

private:
	void prepareSettings();
	void processArguments();

	QStringList readArguments(int argc, char *argv[]) const;
	virtual std::optional<QStringList> readArgumentsHook(
			int argc,
			char *argv[]) const {
		return std::nullopt;
	}

	void init();
	virtual void initHook() {
	}

	virtual bool launchUpdater(UpdaterLaunch action) = 0;

	int executeApplication();

	int _argc;
	char **_argv;
	QStringList _arguments;

	const QString _deviceModel;
	const QString _systemVersion;

	bool _customWorkingDir = false;

};

} // namespace Core
