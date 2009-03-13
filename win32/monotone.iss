#define APPNAME="monotone"
#define APPVERS="0.43dev"
#define BUILDDIR=".."
#define MINGWDIR="\MinGW"

[Setup]
AppName={#APPNAME}
AppVersion={#APPVERS}
AppVerName={#APPNAME} {#APPVERS}
OutputBaseFileName={#APPNAME}-{#APPVERS}-setup
AppCopyright=Copyright © 2002-2009 Graydon Hoare et al.
AppPublisher=The monotone Development Team
AppPublisherURL=http://www.monotone.ca/
DefaultDirName={pf}\{#APPNAME}
DefaultGroupName={#APPNAME}
MinVersion=4.0,4.0
OutputDir=.
AllowNoIcons=1
Compression=lzma/ultra
SolidCompression=yes
LicenseFile="..\COPYING"
ChangesEnvironment=true
WizardImageFile=monotone.bmp

[Files]
Source: "{#BUILDDIR}\mtn.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BUILDDIR}\monotone.html"; DestDir: "{app}\documentation"; Flags: ignoreversion
Source: "{#BUILDDIR}\figures\*.png"; DestDir: "{app}\documentation\figures"; Flags: ignoreversion
Source: "..\COPYING"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\po\de.gmo"; DestDir: "{app}\locale\de\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "..\po\es.gmo"; DestDir: "{app}\locale\es\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "..\po\it.gmo"; DestDir: "{app}\locale\it\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "..\po\sv.gmo"; DestDir: "{app}\locale\sv\LC_MESSAGES"; DestName: "monotone.mo"; Flags: ignoreversion
Source: "{#MINGWDIR}\bin\libiconv-2.dll"; DestDir: "{app}"
Source: "{#MINGWDIR}\local\bin\libidn-11.dll"; DestDir: "{app}"
Source: "{#MINGWDIR}\bin\libintl-8.dll"; DestDir: "{app}"
Source: "{#MINGWDIR}\local\bin\libpcre-0.dll"; DestDir: "{app}"
Source: "{#MINGWDIR}\local\bin\libpcrecpp-0.dll"; DestDir: "{app}"
Source: "{#MINGWDIR}\local\bin\libpcreposix-0.dll"; DestDir: "{app}"
Source: "{#MINGWDIR}\bin\zlib1.dll"; DestDir: "{app}"

[Tasks]
Name: modifypath; Description: "Add monotone to your path"; GroupDescription: "Get up and running"; Flags: unchecked
Name: viewdocs; Description: "View the monotone documentation"; GroupDescription: "Get up and running"

[Run]
Filename: "{app}\documentation\monotone.html"; Tasks: viewdocs; Flags: shellexec nowait; WorkingDir: "{app}\documentation"

[Icons]
Name: "{group}\monotone documentation"; Filename: "{app}\documentation\monotone.html"; WorkingDir: "{app}"

[Code]
function ModPathDir(): String;
begin
  Result := ExpandConstant('{app}');
end;

#include "modpath.iss"

