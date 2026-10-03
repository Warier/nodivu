const path=require('node:path');
const root=path.resolve(__dirname,'../..');
module.exports={
  appId:'org.nodivu.desktop',productName:'Nodivu',asar:false,
  directories:{app:path.join(root,'apps/nodivu-electron'),output:path.join(root,'.local/releases'),buildResources:__dirname},
  files:['*.cjs','ui/**/*','demo/**/*','!package.cjs','!tests/**/*'],
  extraFiles:[
    {from:path.join(root,'target/release/nodivu-app-backend.exe'),to:'resources/app/bin/nodivu-app-backend.exe'},
    ...['mp3','windows-audio'].map(name=>({from:path.join(root,'.local/plugins',name),to:'plugins/'+name})),
    {from:path.join(root,'.local/release-support'),to:'.'}
  ],
  win:{target:['nsis'],executableName:'nodivu-electron',requestedExecutionLevel:'asInvoker'},
  nsis:{oneClick:false,perMachine:false,allowElevation:false,allowToChangeInstallationDirectory:false,
    deleteAppDataOnUninstall:false,runAfterFinish:false,createDesktopShortcut:false,
    include:path.join(__dirname,'migration.nsh'),license:path.join(root,'LICENSE')},
  artifactName:'Nodivu-${version}-windows-${arch}-setup.${ext}',
  publish:[{provider:'github',owner:'Warier',repo:'nodivu',channel:'beta',releaseType:'prerelease'}],
  electronUpdaterCompatibility:'>=2.16',
};
