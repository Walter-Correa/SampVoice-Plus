#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef SampVoiceAsiPath
  #define SampVoiceAsiPath "..\dist\sampvoice.asi"
#endif
#ifndef UalDllPath
  #define UalDllPath "..\build\installer-staging\dinput8.dll"
#endif
#ifndef ThirdPartyNoticesPath
  #define ThirdPartyNoticesPath "..\THIRD-PARTY-NOTICES.txt"
#endif
#ifndef OutputDirPath
  #define OutputDirPath "..\dist"
#endif

#define MyAppName      "SampVoice Plus"
#define MyAppPublisher "Walter Correa"
#define MyAppURL        "https://github.com/Walter-Correa/SampVoice-Plus"
#define MyDownloadURL   "https://github.com/Walter-Correa/SampVoice-Plus/releases/latest"
#define SampDownloadURL "https://www.sa-mp.mp/downloads/"
#define OmpLauncherURL  "https://open.mp/downloads/launcher"
#define KnownUalSha256  "73d6db0481af99b8481e8e6a685dd385e5a3e0e10518bfe97a993f7f368667eb"
#define LegacyAppKey    "Software\Microsoft\Windows\CurrentVersion\Uninstall\{01EA93A6-5255-44FE-ADB9-FDC3EFE89675}_is1"
#ifdef SVPLUS_TEST_APPID
  #define PlusAppGuid     "{D0A1B2C3-4E5F-6A7B-8C9D-0E1F2A3B4C5D}"
  #define PlusAppIdSetup  "{{D0A1B2C3-4E5F-6A7B-8C9D-0E1F2A3B4C5D}"
  #define TestTitleSuffix " (Test)"
#else
  #define PlusAppGuid     "{8F3B4D21-7C9E-4A52-B108-6E4F9D2C51A7}"
  #define PlusAppIdSetup  "{{8F3B4D21-7C9E-4A52-B108-6E4F9D2C51A7}"
  #define TestTitleSuffix ""
#endif
#define PlusAppKey      "Software\Microsoft\Windows\CurrentVersion\Uninstall\" + PlusAppGuid + "_is1"

[Setup]
AppId={#PlusAppIdSetup}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
UsePreviousAppDir=no
DefaultDirName={code:GetInitialGtaPath}
UninstallFilesDir={app}\sampvoice-plus
UninstallDisplayName={#MyAppName} {#MyAppVersion}{#TestTitleSuffix}
UninstallDisplayIcon={uninstallexe}
SetupIconFile=icon.ico
OutputDir={#OutputDirPath}
OutputBaseFilename=SampVoice-Plus-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern dynamic
WizardImageFile=art\wizard-large-100.png,art\wizard-large-125.png,art\wizard-large-150.png,art\wizard-large-175.png,art\wizard-large-200.png,art\wizard-large-225.png,art\wizard-large-250.png
WizardImageFileDynamicDark=art\wizard-large-100.png,art\wizard-large-125.png,art\wizard-large-150.png,art\wizard-large-175.png,art\wizard-large-200.png,art\wizard-large-225.png,art\wizard-large-250.png
WizardSmallImageFile=art\wizard-small-light-100.png,art\wizard-small-light-125.png,art\wizard-small-light-150.png,art\wizard-small-light-175.png,art\wizard-small-light-200.png,art\wizard-small-light-225.png,art\wizard-small-light-250.png
WizardSmallImageFileDynamicDark=art\wizard-small-dark-100.png,art\wizard-small-dark-125.png,art\wizard-small-dark-150.png,art\wizard-small-dark-175.png,art\wizard-small-dark-200.png,art\wizard-small-dark-225.png,art\wizard-small-dark-250.png
WizardImageBackColor=#10151F
WizardImageBackColorDynamicDark=#10151F
WizardSizePercent=110,122
DisableWelcomePage=no
DisableProgramGroupPage=yes
DirExistsWarning=no
AppendDefaultDirName=no
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "id"; MessagesFile: "languages\Indonesian.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "hu"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "ro"; MessagesFile: "languages\Romanian.isl"
Name: "vi"; MessagesFile: "languages\Vietnamese.isl"
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "bg"; MessagesFile: "compiler:Languages\Bulgarian.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[CustomMessages]
; --- English (en) ---
en.MissingGtaExe=gta_sa.exe was not found in this folder. Please select your GTA San Andreas 1.0 directory.
en.MissingSamp=SA-MP was not found in this folder (samp.dll and/or bass.dll missing).%nPlease install SA-MP 0.3.7 (R1, R2, R3, R4, R5) or SA-MP 0.3.DL first, then run this installer again.
en.DownloadSampLink=Download SA-MP (sa-mp.mp/downloads/)
en.DownloadOmpLink=or use the open.mp Launcher (open.mp/downloads/launcher)
en.DetectPageTitle=Environment Detection
en.DetectPageSubTitle=Checking SA-MP version, ASI loader, and existing voice plugins
en.SampDetectedFmt=SA-MP detected: %1 (compatible)
en.SampUnknownWarn=SA-MP build could not be identified (%1). SampVoice Plus will validate the build at startup and disable voice automatically if unsupported.
en.LoaderFoundFmt=ASI loader detected: %1 (no loader will be added).
en.LoaderInstallUal=No ASI loader detected: Ultimate ASI Loader (MIT) will be installed as dinput8.dll.
en.LoaderConflictText=An unknown third-party dinput8.dll was found in this folder and no other ASI loader was detected. Overwriting it may break existing mods. Installation cannot continue automatically; remove/rename dinput8.dll or use Manual Install.
en.PlusRepairFmt=SampVoice Plus %1 is already installed: files will be verified and repaired.
en.PlusUpdateFmt=Updating SampVoice Plus from %1 to %2.
en.PlusUpdateUnknownVerFmt=Updating SampVoice Plus from an earlier version to %1.
en.LegacyDetectedHeaderFmt=Legacy SampVoice detected: %1
en.LegacyManualTag=manually installed
en.LegacyBackupTitle=Back up (recommended)
en.LegacyBackupDesc=Legacy SampVoice files are saved. Uninstalling SampVoice Plus restores everything exactly as it was.
en.LegacyRemoveTitle=Uninstall legacy SampVoice
en.LegacyRemoveDesc=Permanently removes legacy SampVoice. Uninstalling SampVoice Plus later will NOT bring it back.
en.LegacyRemoveConfigCheck=Also delete legacy SampVoice settings (Documents folder)
en.SharedFilesNote=Shared game files (bass.dll, vorbisFile.dll, vorbisHooked.dll) are kept untouched.
en.DowngradePromptFmt=A newer version (%1) is already installed. Install %2 anyway?
en.UninstallKeepChangedWarn=The following file was modified after installation and was kept untouched:%n%1
en.UninstallRemoveIni=Do you also want to remove your SampVoice Plus settings and logs in Documents\GTA San Andreas User Files\SAMP\sampvoice\?
en.CloseGtaBeforeContinue=Please close GTA San Andreas before continuing.
en.UninstallRestoreFailedWarn=Some files could not be restored or removed (make sure GTA San Andreas is closed).%nYour original files are still saved in:%n%1
en.SummaryPlusRepairFmt=SampVoice Plus: verify and repair %1
en.SummaryPlusUpdateFmt=SampVoice Plus: update from %1 to %2
en.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: update from an earlier version to %1
en.SummaryLegacyBackup=Legacy SampVoice: back up and replace
en.SummaryLegacyRemove=Legacy SampVoice: uninstall permanently
en.SummaryLegacyRemoveCfg=Legacy SampVoice settings (Documents): delete
en.WelcomeDownloadPre=Always download and update from the
en.FinishedDownloadPre=New versions are published on the
en.WelcomeDownloadLink=official SampVoice Plus page
en.FinishedDownloadLink=official SampVoice Plus page

; --- Bahasa Indonesia (id) ---
id.MissingGtaExe=gta_sa.exe tidak ditemukan di folder ini. Pilih folder GTA San Andreas 1.0 Anda.
id.MissingSamp=SA-MP tidak ditemukan di folder ini (samp.dll atau bass.dll tidak ada).%nPasang SA-MP 0.3.7 (R1-R5) atau 0.3.DL terlebih dahulu, lalu jalankan pemasang ini lagi.
id.DownloadSampLink=Unduh SA-MP (sa-mp.mp/downloads/)
id.DownloadOmpLink=atau gunakan open.mp Launcher
id.DetectPageTitle=Deteksi Lingkungan
id.DetectPageSubTitle=Memeriksa versi SA-MP, ASI loader, dan plugin suara yang ada
id.SampDetectedFmt=SA-MP terdeteksi: %1 (kompatibel)
id.SampUnknownWarn=Build SA-MP tidak dikenali (%1). SampVoice Plus akan memeriksa saat game berjalan.
id.LoaderFoundFmt=ASI loader terdeteksi: %1 (tidak perlu memasang loader).
id.LoaderInstallUal=ASI loader tidak ditemukan: Ultimate ASI Loader (MIT) akan dipasang sebagai dinput8.dll.
id.LoaderConflictText=dinput8.dll pihak ketiga tidak dikenal ditemukan dan tidak ada ASI loader lain. Pemasangan dibatalkan agar mod Anda aman.
id.PlusRepairFmt=SampVoice Plus %1 sudah terpasang: berkas akan diperiksa dan diperbaiki.
id.PlusUpdateFmt=Memperbarui SampVoice Plus dari %1 ke %2.
id.PlusUpdateUnknownVerFmt=Memperbarui SampVoice Plus dari versi sebelumnya ke %1.
id.LegacyDetectedHeaderFmt=SampVoice lama terdeteksi: %1
id.LegacyManualTag=dipasang manual
id.LegacyBackupTitle=Cadangkan (disarankan)
id.LegacyBackupDesc=Berkas SampVoice lama disimpan. Menghapus SampVoice Plus akan mengembalikan semuanya seperti semula.
id.LegacyRemoveTitle=Hapus SampVoice lama
id.LegacyRemoveDesc=Menghapus SampVoice lama secara permanen. Menghapus SampVoice Plus nanti TIDAK akan mengembalikannya.
id.LegacyRemoveConfigCheck=Hapus juga pengaturan SampVoice lama (folder Documents)
id.SharedFilesNote=Berkas bersama game (bass.dll, vorbisFile.dll, vorbisHooked.dll) tetap dipertahankan.
id.DowngradePromptFmt=Versi yang lebih baru (%1) sudah terpasang. Tetap pasang %2?
id.UninstallKeepChangedWarn=Berkas berikut diubah setelah pemasangan dan tidak dihapus:%n%1
id.UninstallRemoveIni=Hapus juga pengaturan Anda di Documents\GTA San Andreas User Files\SAMP\sampvoice\?
id.CloseGtaBeforeContinue=Harap tutup GTA San Andreas sebelum melanjutkan.
id.UninstallRestoreFailedWarn=Beberapa berkas tidak dapat dipulihkan atau dihapus (pastikan GTA San Andreas telah ditutup).%nBerkas asli Anda masih tersimpan di:%n%1
id.SummaryPlusRepairFmt=SampVoice Plus: periksa dan perbaiki %1
id.SummaryPlusUpdateFmt=SampVoice Plus: perbarui dari %1 ke %2
id.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: perbarui dari versi sebelumnya ke %1
id.SummaryLegacyBackup=SampVoice lama: cadangkan dan ganti
id.SummaryLegacyRemove=SampVoice lama: hapus permanen
id.SummaryLegacyRemoveCfg=Pengaturan SampVoice lama (Documents): hapus
id.WelcomeDownloadPre=Selalu unduh dan perbarui dari
id.FinishedDownloadPre=Versi baru tersedia di
id.WelcomeDownloadLink=halaman resmi SampVoice Plus
id.FinishedDownloadLink=halaman resmi SampVoice Plus

; --- Deutsch (de) ---
de.MissingGtaExe=gta_sa.exe wurde in diesem Ordner nicht gefunden. Bitte wählen Sie Ihren GTA San Andreas 1.0 Ordner.
de.MissingSamp=SA-MP wurde in diesem Ordner nicht gefunden (samp.dll / bass.dll fehlt).%nBitte installieren Sie zuerst SA-MP 0.3.7 (R1-R5) oder 0.3.DL und starten Sie dieses Setup erneut.
de.DownloadSampLink=SA-MP herunterladen (sa-mp.mp/downloads/)
de.DownloadOmpLink=oder den open.mp Launcher verwenden
de.DetectPageTitle=Umgebungserkennung
de.DetectPageSubTitle=Prüfung von SA-MP-Version, ASI Loader und vorhandenen Plugins
de.SampDetectedFmt=SA-MP erkannt: %1 (kompatibel)
de.SampUnknownWarn=Unbekannter SA-MP Build (%1). SampVoice Plus prüft die Kompatibilität beim Spielstart.
de.LoaderFoundFmt=ASI Loader erkannt: %1 (kein Loader erforderlich).
de.LoaderInstallUal=Kein ASI Loader gefunden: Ultimate ASI Loader (MIT) wird als dinput8.dll installiert.
de.LoaderConflictText=Eine unbekannte dinput8.dll wurde gefunden und kein anderer ASI Loader erkannt. Abbruch zum Schutz bestehender Mods.
de.PlusRepairFmt=SampVoice Plus %1 ist bereits installiert: Dateien werden geprüft und repariert.
de.PlusUpdateFmt=SampVoice Plus wird von %1 auf %2 aktualisiert.
de.PlusUpdateUnknownVerFmt=SampVoice Plus wird von einer früheren Version auf %1 aktualisiert.
de.LegacyDetectedHeaderFmt=Altes SampVoice erkannt: %1
de.LegacyManualTag=manuell installiert
de.LegacyBackupTitle=Sicherung erstellen (empfohlen)
de.LegacyBackupDesc=Dateien des alten SampVoice werden gesichert. Beim Deinstallieren wird alles genau wiederhergestellt.
de.LegacyRemoveTitle=Altes SampVoice deinstallieren
de.LegacyRemoveDesc=Entfernt das alte SampVoice dauerhaft. Bei späterer Deinstallation kehrt es NICHT zurück.
de.LegacyRemoveConfigCheck=Auch alte SampVoice-Einstellungen löschen (Ordner Dokumente)
de.SharedFilesNote=Gemeinsame Spieldateien (bass.dll, vorbisFile.dll, vorbisHooked.dll) bleiben erhalten.
de.DowngradePromptFmt=Eine neuere Version (%1) ist bereits installiert. %2 trotzdem installieren?
de.UninstallKeepChangedWarn=Diese Datei wurde nach der Installation geändert und nicht gelöscht:%n%1
de.UninstallRemoveIni=Möchten Sie auch die Einstellungen unter Documents\GTA San Andreas User Files\SAMP\sampvoice\ entfernen?
de.CloseGtaBeforeContinue=Bitte schließen Sie GTA San Andreas, bevor Sie fortfahren.
de.UninstallRestoreFailedWarn=Einige Dateien konnten nicht wiederhergestellt oder entfernt werden (stellen Sie sicher, dass GTA San Andreas geschlossen ist).%nIhre Originaldateien befinden sich weiterhin in:%n%1
de.SummaryPlusRepairFmt=SampVoice Plus: %1 prüfen und reparieren
de.SummaryPlusUpdateFmt=SampVoice Plus: Aktualisierung von %1 auf %2
de.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: Aktualisierung von einer früheren Version auf %1
de.SummaryLegacyBackup=Altes SampVoice: sichern und ersetzen
de.SummaryLegacyRemove=Altes SampVoice: dauerhaft entfernen
de.SummaryLegacyRemoveCfg=Alte SampVoice-Einstellungen (Dokumente): löschen
de.WelcomeDownloadPre=Immer herunterladen und aktualisieren über die
de.FinishedDownloadPre=Neue Versionen erscheinen auf der
de.WelcomeDownloadLink=offizielle SampVoice-Plus-Seite
de.FinishedDownloadLink=offiziellen SampVoice-Plus-Seite

; --- Español (es) ---
es.MissingGtaExe=No se encontró gta_sa.exe en esta carpeta. Seleccione su carpeta de GTA San Andreas 1.0.
es.MissingSamp=No se encontró SA-MP en esta carpeta (falta samp.dll o bass.dll).%nInstale primero SA-MP 0.3.7 (R1-R5) o 0.3.DL y vuelva a ejecutar este instalador.
es.DownloadSampLink=Descargar SA-MP (sa-mp.mp/downloads/)
es.DownloadOmpLink=o usar el Launcher de open.mp
es.DetectPageTitle=Detección del Entorno
es.DetectPageSubTitle=Comprobando versión de SA-MP, ASI loader y plugins existentes
es.SampDetectedFmt=SA-MP detectado: %1 (compatible)
es.SampUnknownWarn=Compilación de SA-MP desconocida (%1). SampVoice Plus la verificará al iniciar el juego.
es.LoaderFoundFmt=ASI loader detectado: %1 (no se instalará otro).
es.LoaderInstallUal=No se encontró ASI loader: se instalará Ultimate ASI Loader (MIT) como dinput8.dll.
es.LoaderConflictText=Se encontró un dinput8.dll de terceros desconocido sin otro ASI loader. Se cancela para no sobrescribir sus mods.
es.PlusRepairFmt=SampVoice Plus %1 ya está instalado: los archivos se verificarán y repararán.
es.PlusUpdateFmt=Actualizando SampVoice Plus de %1 a %2.
es.PlusUpdateUnknownVerFmt=Actualizando SampVoice Plus de una versión anterior a %1.
es.LegacyDetectedHeaderFmt=SampVoice antiguo encontrado: %1
es.LegacyManualTag=instalado manualmente
es.LegacyBackupTitle=Hacer copia de seguridad (recomendado)
es.LegacyBackupDesc=Se guardan los archivos del SampVoice antiguo. Al desinstalar SampVoice Plus, todo vuelve igual.
es.LegacyRemoveTitle=Desinstalar el SampVoice antiguo
es.LegacyRemoveDesc=Elimina el SampVoice antiguo para siempre. Al desinstalar SampVoice Plus después, NO volverá.
es.LegacyRemoveConfigCheck=Eliminar también la configuración del SampVoice antiguo (carpeta Documentos)
es.SharedFilesNote=Los archivos compartidos con el juego (bass.dll, vorbisFile.dll, vorbisHooked.dll) se conservan.
es.DowngradePromptFmt=Ya hay instalada una versión más nueva (%1). ¿Instalar %2 de todos modos?
es.UninstallKeepChangedWarn=Este archivo fue modificado tras la instalación y se conservó intacto:%n%1
es.UninstallRemoveIni=¿Desea eliminar también su configuración en Documents\GTA San Andreas User Files\SAMP\sampvoice\?
es.CloseGtaBeforeContinue=Cierre GTA San Andreas antes de continuar.
es.UninstallRestoreFailedWarn=Algunos archivos no pudieron restaurarse ni eliminarse (asegúrese de cerrar GTA San Andreas).%nSus archivos originales siguen guardados en:%n%1
es.SummaryPlusRepairFmt=SampVoice Plus: verificar y reparar %1
es.SummaryPlusUpdateFmt=SampVoice Plus: actualizar de %1 a %2
es.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: actualizar de una versión anterior a %1
es.SummaryLegacyBackup=SampVoice antiguo: respaldar y reemplazar
es.SummaryLegacyRemove=SampVoice antiguo: eliminar permanentemente
es.SummaryLegacyRemoveCfg=Configuración del SampVoice antiguo (Documentos): eliminar
es.WelcomeDownloadPre=Descargue y actualice siempre desde la
es.FinishedDownloadPre=Las nuevas versiones están en la
es.WelcomeDownloadLink=página oficial de SampVoice Plus
es.FinishedDownloadLink=página oficial de SampVoice Plus

; --- Français (fr) ---
fr.MissingGtaExe=gta_sa.exe est introuvable dans ce dossier. Veuillez sélectionner votre dossier GTA San Andreas 1.0.
fr.MissingSamp=SA-MP est introuvable dans ce dossier (samp.dll / bass.dll manquant).%nInstallez d'abord SA-MP 0.3.7 (R1-R5) ou 0.3.DL puis relancez l'installateur.
fr.DownloadSampLink=Télécharger SA-MP (sa-mp.mp/downloads/)
fr.DownloadOmpLink=ou utiliser le Launcher open.mp
fr.DetectPageTitle=Détection de l'environnement
fr.DetectPageSubTitle=Vérification de SA-MP, du chargeur ASI et des plugins existants
fr.SampDetectedFmt=SA-MP détecté : %1 (compatible)
fr.SampUnknownWarn=Version SA-MP inconnue (%1). SampVoice Plus vérifiera la compatibilité au lancement.
fr.LoaderFoundFmt=Chargeur ASI détecté : %1 (aucun chargeur ajouté).
fr.LoaderInstallUal=Aucun chargeur ASI détecté : Ultimate ASI Loader (MIT) sera installé en tant que dinput8.dll.
fr.LoaderConflictText=Un fichier dinput8.dll tiers inconnu est présent sans autre chargeur ASI. Annulation par sécurité.
fr.PlusRepairFmt=SampVoice Plus %1 est déjà installé : les fichiers seront vérifiés et réparés.
fr.PlusUpdateFmt=Mise à jour de SampVoice Plus de %1 vers %2.
fr.PlusUpdateUnknownVerFmt=Mise à jour de SampVoice Plus d'une version antérieure vers %1.
fr.LegacyDetectedHeaderFmt=Ancien SampVoice détecté : %1
fr.LegacyManualTag=installé manuellement
fr.LegacyBackupTitle=Sauvegarder (recommandé)
fr.LegacyBackupDesc=Les fichiers de l'ancien SampVoice sont conservés. La désinstallation restaure tout à l'identique.
fr.LegacyRemoveTitle=Désinstaller l'ancien SampVoice
fr.LegacyRemoveDesc=Supprime définitivement l'ancien SampVoice. La désinstallation ultérieure ne le rétablira PAS.
fr.LegacyRemoveConfigCheck=Supprimer aussi les paramètres de l'ancien SampVoice (dossier Documents)
fr.SharedFilesNote=Les fichiers partagés avec le jeu (bass.dll, vorbisFile.dll, vorbisHooked.dll) sont conservés.
fr.DowngradePromptFmt=Une version plus récente (%1) est déjà installée. Installer %2 quand même ?
fr.UninstallKeepChangedWarn=Ce fichier a été modifié après l'installation et a été conservé :%n%1
fr.UninstallRemoveIni=Voulez-vous aussi supprimer vos paramètres dans Documents\GTA San Andreas User Files\SAMP\sampvoice\ ?
fr.CloseGtaBeforeContinue=Veuillez fermer GTA San Andreas avant de continuer.
fr.UninstallRestoreFailedWarn=Certains fichiers n'ont pas pu être restaurés ou supprimés (vérifiez que GTA San Andreas est fermé).%nVos fichiers d'origine restent enregistrés dans :%n%1
fr.SummaryPlusRepairFmt=SampVoice Plus : vérifier et réparer %1
fr.SummaryPlusUpdateFmt=SampVoice Plus : mise à jour de %1 vers %2
fr.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus : mise à jour d'une version antérieure vers %1
fr.SummaryLegacyBackup=Ancien SampVoice : sauvegarder et remplacer
fr.SummaryLegacyRemove=Ancien SampVoice : supprimer définitivement
fr.SummaryLegacyRemoveCfg=Paramètres de l'ancien SampVoice (Documents) : supprimer
fr.WelcomeDownloadPre=Téléchargez et mettez toujours à jour via la
fr.FinishedDownloadPre=Les nouvelles versions se trouvent sur la
fr.WelcomeDownloadLink=page officielle de SampVoice Plus
fr.FinishedDownloadLink=page officielle de SampVoice Plus

; --- Italiano (it) ---
it.MissingGtaExe=gta_sa.exe non trovato in questa cartella. Seleziona la cartella di GTA San Andreas 1.0.
it.MissingSamp=SA-MP non trovato in questa cartella (samp.dll o bass.dll mancante).%nInstalla prima SA-MP 0.3.7 (R1-R5) o 0.3.DL e riavvia il programma di installazione.
it.DownloadSampLink=Scarica SA-MP (sa-mp.mp/downloads/)
it.DownloadOmpLink=oppure usa il Launcher open.mp
it.DetectPageTitle=Rilevamento Ambiente
it.DetectPageSubTitle=Verifica versione SA-MP, ASI loader e plugin esistenti
it.SampDetectedFmt=SA-MP rilevato: %1 (compatibile)
it.SampUnknownWarn=Build SA-MP non riconosciuta (%1). SampVoice Plus verificherà all'avvio.
it.LoaderFoundFmt=ASI loader rilevato: %1 (nessun loader aggiunto).
it.LoaderInstallUal=Nessun ASI loader trovato: verrà installato Ultimate ASI Loader (MIT) come dinput8.dll.
it.LoaderConflictText=Trovato dinput8.dll sconosciuto di terze parti senza altri loader ASI. Installazione annullata.
it.PlusRepairFmt=SampVoice Plus %1 è già installato: i file verranno verificati e riparati.
it.PlusUpdateFmt=Aggiornamento di SampVoice Plus da %1 a %2.
it.PlusUpdateUnknownVerFmt=Aggiornamento di SampVoice Plus da una versione precedente a %1.
it.LegacyDetectedHeaderFmt=Vecchio SampVoice rilevato: %1
it.LegacyManualTag=installato manualmente
it.LegacyBackupTitle=Esegui backup (consigliato)
it.LegacyBackupDesc=I file del vecchio SampVoice vengono salvati. Disinstallando SampVoice Plus tutto torna come prima.
it.LegacyRemoveTitle=Disinstalla il vecchio SampVoice
it.LegacyRemoveDesc=Rimuove per sempre il vecchio SampVoice. Disinstallando SampVoice Plus NON tornerà.
it.LegacyRemoveConfigCheck=Elimina anche le impostazioni del vecchio SampVoice (cartella Documenti)
it.SharedFilesNote=I file condivisi con il gioco (bass.dll, vorbisFile.dll, vorbisHooked.dll) vengono mantenuti.
it.DowngradePromptFmt=È già installata una versione più recente (%1). Installare comunque %2?
it.UninstallKeepChangedWarn=Questo file è stato modificato dopo l'installazione ed è stato mantenuto:%n%1
it.UninstallRemoveIni=Rimuovere anche le impostazioni in Documents\GTA San Andreas User Files\SAMP\sampvoice\?
it.CloseGtaBeforeContinue=Chiudi GTA San Andreas prima di continuare.
it.UninstallRestoreFailedWarn=Alcuni file non possono essere ripristinati o rimossi (assicurati che GTA San Andreas sia chiuso).%nI file originali restano salvati in:%n%1
it.SummaryPlusRepairFmt=SampVoice Plus: verifica e ripara %1
it.SummaryPlusUpdateFmt=SampVoice Plus: aggiorna da %1 a %2
it.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: aggiorna da una versione precedente a %1
it.SummaryLegacyBackup=Vecchio SampVoice: backup e sostituzione
it.SummaryLegacyRemove=Vecchio SampVoice: rimozione definitiva
it.SummaryLegacyRemoveCfg=Impostazioni vecchio SampVoice (Documenti): elimina
it.WelcomeDownloadPre=Scarica e aggiorna sempre dalla
it.FinishedDownloadPre=Le nuove versioni si trovano sulla
it.WelcomeDownloadLink=pagina ufficiale di SampVoice Plus
it.FinishedDownloadLink=pagina ufficiale di SampVoice Plus

; --- Magyar (hu) ---
hu.MissingGtaExe=A gta_sa.exe nem található ebben a mappában. Válaszd ki a GTA San Andreas 1.0 mappáját.
hu.MissingSamp=A SA-MP nem található ebben a mappában (hiányzó samp.dll / bass.dll).%nElőbb telepítsd a SA-MP 0.3.7 (R1-R5) vagy 0.3.DL verziót, majd futtasd újra a telepítőt.
hu.DownloadSampLink=SA-MP letöltése (sa-mp.mp/downloads/)
hu.DownloadOmpLink=vagy használd az open.mp Launchert
hu.DetectPageTitle=Környezet észlelése
hu.DetectPageSubTitle=SA-MP verzió, ASI loader és meglévő hangbővítmények ellenőrzése
hu.SampDetectedFmt=SA-MP észlelve: %1 (kompatibilis)
hu.SampUnknownWarn=Ismeretlen SA-MP build (%1). A SampVoice Plus indításkor ellenőrzi.
hu.LoaderFoundFmt=ASI loader észlelve: %1 (nem szükséges új loader).
hu.LoaderInstallUal=Nincs ASI loader: az Ultimate ASI Loader (MIT) telepítésre kerül dinput8.dll néven.
hu.LoaderConflictText=Ismeretlen külső dinput8.dll található a mappában más ASI loader nélkül. A telepítés megszakadt.
hu.PlusRepairFmt=A SampVoice Plus %1 már telepítve van: a fájlok ellenőrzése és javítása történik.
hu.PlusUpdateFmt=SampVoice Plus frissítése: %1 -> %2.
hu.PlusUpdateUnknownVerFmt=SampVoice Plus frissítése egy korábbi verzióról erre: %1.
hu.LegacyDetectedHeaderFmt=Régi SampVoice észlelve: %1
hu.LegacyManualTag=kézzel telepítve
hu.LegacyBackupTitle=Biztonsági mentés készítése (ajánlott)
hu.LegacyBackupDesc=A régi SampVoice fájlok megőrzésre kerülnek. Eltávolításkor minden pontosan visszaáll.
hu.LegacyRemoveTitle=Régi SampVoice eltávolítása
hu.LegacyRemoveDesc=Végleg törli a régi SampVoice-t. A SampVoice Plus későbbi törlésekor NEM áll vissza.
hu.LegacyRemoveConfigCheck=A régi SampVoice beállításainak törlése is (Dokumentumok mappa)
hu.SharedFilesNote=A játék közös fájljai (bass.dll, vorbisFile.dll, vorbisHooked.dll) megmaradnak.
hu.DowngradePromptFmt=Már egy újabb verzió (%1) van telepítve. Mégis telepíti a %2 verziót?
hu.UninstallKeepChangedWarn=Ez a fájl telepítés után megváltozott, ezért megmaradt:%n%1
hu.UninstallRemoveIni=Törölni szeretnéd a beállításokat a Documents\GTA San Andreas User Files\SAMP\sampvoice\ mappából is?
hu.CloseGtaBeforeContinue=Kérjük, zárd be a GTA San Andreas-t a folytatás előtt.
hu.UninstallRestoreFailedWarn=Egyes fájlokat nem sikerült visszaállítani vagy eltávolítani (győződj meg róla, hogy a GTA San Andreas be van zárva).%nAz eredeti fájlok továbbra is itt találhatók:%n%1
hu.SummaryPlusRepairFmt=SampVoice Plus: %1 ellenőrzése és javítása
hu.SummaryPlusUpdateFmt=SampVoice Plus: frissítés (%1 -> %2)
hu.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: frissítés egy korábbi verzióról erre: %1
hu.SummaryLegacyBackup=Régi SampVoice: biztonsági mentés és csere
hu.SummaryLegacyRemove=Régi SampVoice: végleges eltávolítás
hu.SummaryLegacyRemoveCfg=Régi SampVoice beállítások (Dokumentumok): törlés
hu.WelcomeDownloadPre=Mindig innen töltsd le és frissítsd:
hu.FinishedDownloadPre=Az új verziók itt érhetők el:
hu.WelcomeDownloadLink=SampVoice Plus hivatalos oldala
hu.FinishedDownloadLink=SampVoice Plus hivatalos oldala

; --- Polski (pl) ---
pl.MissingGtaExe=Nie znaleziono gta_sa.exe w tym folderze. Wybierz folder gry GTA San Andreas 1.0.
pl.MissingSamp=Nie znaleziono SA-MP w tym folderze (brak samp.dll lub bass.dll).%nZainstaluj najpierw SA-MP 0.3.7 (R1-R5) lub 0.3.DL, a następnie uruchom instalator ponownie.
pl.DownloadSampLink=Pobierz SA-MP (sa-mp.mp/downloads/)
pl.DownloadOmpLink=lub użyj open.mp Launcher
pl.DetectPageTitle=Wykrywanie środowiska
pl.DetectPageSubTitle=Sprawdzanie wersji SA-MP, loadera ASI oraz istniejących wtyczek
pl.SampDetectedFmt=Wykryto SA-MP: %1 (zgodny)
pl.SampUnknownWarn=Nieznana wersja SA-MP (%1). SampVoice Plus sprawdzi ją przy uruchomieniu gry.
pl.LoaderFoundFmt=Wykryto ASI loader: %1 (dodatkowy loader nie jest wymagany).
pl.LoaderInstallUal=Brak ASI loadera: Ultimate ASI Loader (MIT) zostanie zainstalowany jako dinput8.dll.
pl.LoaderConflictText=Wykryto nieznany plik dinput8.dll bez innego loadera ASI. Instalacja przerwana dla ochrony modów.
pl.PlusRepairFmt=SampVoice Plus %1 jest już zainstalowany: pliki zostaną zweryfikowane i naprawione.
pl.PlusUpdateFmt=Aktualizacja SampVoice Plus z %1 do %2.
pl.PlusUpdateUnknownVerFmt=Aktualizacja SampVoice Plus ze starszej wersji do %1.
pl.LegacyDetectedHeaderFmt=Wykryto stary SampVoice: %1
pl.LegacyManualTag=zainstalowany ręcznie
pl.LegacyBackupTitle=Utwórz kopię zapasową (zalecane)
pl.LegacyBackupDesc=Stare pliki SampVoice zostaną zachowane. Odinstalowanie SampVoice Plus przywróci stan pierwotny.
pl.LegacyRemoveTitle=Odinstaluj stary SampVoice
pl.LegacyRemoveDesc=Usuwa stary SampVoice na stałe. Późniejsze odinstalowanie SampVoice Plus go NIE przywróci.
pl.LegacyRemoveConfigCheck=Usuń również ustawienia starego SampVoice (folder Dokumenty)
pl.SharedFilesNote=Współdzielone pliki gry (bass.dll, vorbisFile.dll, vorbisHooked.dll) pozostają nienaruszone.
pl.DowngradePromptFmt=Nowsza wersja (%1) jest już zainstalowana. Czy mimo to zainstalować %2?
pl.UninstallKeepChangedWarn=Ten plik został zmieniony po instalacji i pozostał nienaruszony:%n%1
pl.UninstallRemoveIni=Czy usunąć również ustawienia z Documents\GTA San Andreas User Files\SAMP\sampvoice\?
pl.CloseGtaBeforeContinue=Zamknij GTA San Andreas przed kontynuowaniem.
pl.UninstallRestoreFailedWarn=Niektórych plików nie udało się przywrócić ani usunąć (upewnij się, że GTA San Andreas jest zamknięte).%nOryginalne pliki nadal znajdują się w:%n%1
pl.SummaryPlusRepairFmt=SampVoice Plus: weryfikacja i naprawa %1
pl.SummaryPlusUpdateFmt=SampVoice Plus: aktualizacja z %1 do %2
pl.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: aktualizacja ze starszej wersji do %1
pl.SummaryLegacyBackup=Stary SampVoice: kopia zapasowa i zamiana
pl.SummaryLegacyRemove=Stary SampVoice: trwałe usunięcie
pl.SummaryLegacyRemoveCfg=Ustawienia starego SampVoice (Dokumenty): usuń
pl.WelcomeDownloadPre=Zawsze pobieraj i aktualizuj przez
pl.FinishedDownloadPre=Nowe wersje są publikowane na
pl.WelcomeDownloadLink=oficjalną stronę SampVoice Plus
pl.FinishedDownloadLink=oficjalnej stronie SampVoice Plus

; --- Português (pt) ---
pt.MissingGtaExe=O arquivo gta_sa.exe não foi encontrado nesta pasta. Selecione a pasta do GTA San Andreas 1.0.
pt.MissingSamp=SA-MP não encontrado nesta pasta (samp.dll e/ou bass.dll ausente).%nInstale primeiro o SA-MP 0.3.7 (qualquer versão: R1, R2, R3, R4 ou R5) ou o SA-MP 0.3.DL, e depois execute este instalador novamente.
pt.DownloadSampLink=Baixar SA-MP (sa-mp.mp/downloads/)
pt.DownloadOmpLink=ou use o Launcher open.mp
pt.DetectPageTitle=Detecção do Ambiente
pt.DetectPageSubTitle=Verificando versão do SA-MP, ASI loader e plugins de voz existentes
pt.SampDetectedFmt=SA-MP detectado: %1 (compatível)
pt.SampUnknownWarn=Build do SA-MP desconhecida (%1). O SampVoice Plus validará ao iniciar o jogo e desativará a voz se não for suportada.
pt.LoaderFoundFmt=ASI loader detectado: %1 (nenhum loader será instalado).
pt.LoaderInstallUal=Nenhum ASI loader detectado: o Ultimate ASI Loader (MIT) será instalado como dinput8.dll.
pt.LoaderConflictText=Um dinput8.dll desconhecido de terceiros foi encontrado sem outro ASI loader. A instalação foi cancelada para não sobrescrever seus mods.
pt.PlusRepairFmt=SampVoice Plus %1 já instalado: os arquivos serão verificados e reparados.
pt.PlusUpdateFmt=Atualizando SampVoice Plus de %1 para %2.
pt.PlusUpdateUnknownVerFmt=Atualizando SampVoice Plus de uma versão anterior para %1.
pt.LegacyDetectedHeaderFmt=SampVoice antigo encontrado: %1
pt.LegacyManualTag=instalado manualmente
pt.LegacyBackupTitle=Fazer backup (recomendado)
pt.LegacyBackupDesc=Os arquivos do SampVoice antigo são guardados. Ao desinstalar o SampVoice Plus, tudo volta exatamente como era.
pt.LegacyRemoveTitle=Desinstalar o SampVoice antigo
pt.LegacyRemoveDesc=Remove o SampVoice antigo para sempre. Ao desinstalar o SampVoice Plus depois, ele NÃO volta.
pt.LegacyRemoveConfigCheck=Apagar também as configurações do SampVoice antigo (pasta Documentos)
pt.SharedFilesNote=Os arquivos compartilhados com o jogo (bass.dll, vorbisFile.dll, vorbisHooked.dll) são mantidos.
pt.DowngradePromptFmt=Uma versão mais nova (%1) já está instalada. Instalar a %2 mesmo assim?
pt.UninstallKeepChangedWarn=O arquivo abaixo foi modificado após a instalação e foi preservado:%n%1
pt.UninstallRemoveIni=Deseja também remover suas configurações em Documents\GTA San Andreas User Files\SAMP\sampvoice\?
pt.CloseGtaBeforeContinue=Feche o GTA San Andreas antes de continuar.
pt.UninstallRestoreFailedWarn=Alguns arquivos não puderam ser restaurados ou removidos (verifique se o GTA San Andreas está fechado).%nSeus arquivos originais continuam salvos em:%n%1
pt.SummaryPlusRepairFmt=SampVoice Plus: verificar e reparar %1
pt.SummaryPlusUpdateFmt=SampVoice Plus: atualizar de %1 para %2
pt.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: atualizar de uma versão anterior para %1
pt.SummaryLegacyBackup=SampVoice antigo: fazer backup e substituir
pt.SummaryLegacyRemove=SampVoice antigo: desinstalar para sempre
pt.SummaryLegacyRemoveCfg=Configurações do SampVoice antigo (Documentos): apagar
pt.WelcomeDownloadPre=Baixe e atualize sempre pela
pt.FinishedDownloadPre=Novas versões ficam na
pt.WelcomeDownloadLink=página oficial do SampVoice Plus
pt.FinishedDownloadLink=página oficial do SampVoice Plus

; --- Română (ro) ---
ro.MissingGtaExe=gta_sa.exe nu a fost găsit în acest folder. Selectați folderul GTA San Andreas 1.0.
ro.MissingSamp=SA-MP nu a fost găsit în acest folder (lipsește samp.dll / bass.dll).%nInstalați mai întâi SA-MP 0.3.7 (R1-R5) sau 0.3.DL, apoi rulați din nou instalatorul.
ro.DownloadSampLink=Descarcă SA-MP (sa-mp.mp/downloads/)
ro.DownloadOmpLink=sau folosește open.mp Launcher
ro.DetectPageTitle=Detectare Mediu
ro.DetectPageSubTitle=Verificare versiune SA-MP, ASI loader și plugin-uri existente
ro.SampDetectedFmt=SA-MP detectat: %1 (compatibil)
ro.SampUnknownWarn=Versiune SA-MP necunoscută (%1). SampVoice Plus va verifica compatibilitatea la pornire.
ro.LoaderFoundFmt=ASI loader detectat: %1 (nu se adaugă alt loader).
ro.LoaderInstallUal=Niciun ASI loader găsit: Ultimate ASI Loader (MIT) va fi instalat ca dinput8.dll.
ro.LoaderConflictText=A fost găsit un dinput8.dll terț necunoscut fără alt ASI loader. Instalarea s-a oprit pentru siguranță.
ro.PlusRepairFmt=SampVoice Plus %1 este deja instalat: fișierele vor fi verificate și reparate.
ro.PlusUpdateFmt=Actualizare SampVoice Plus de la %1 la %2.
ro.PlusUpdateUnknownVerFmt=Actualizare SampVoice Plus de la o versiune anterioară la %1.
ro.LegacyDetectedHeaderFmt=SampVoice vechi detectat: %1
ro.LegacyManualTag=instalat manual
ro.LegacyBackupTitle=Fă backup (recomandat)
ro.LegacyBackupDesc=Fișierele SampVoice vechi sunt păstrate. La dezinstalarea SampVoice Plus, totul revine exact cum era.
ro.LegacyRemoveTitle=Dezinstalează vechiul SampVoice
ro.LegacyRemoveDesc=Șterge definitiv vechiul SampVoice. La dezinstalarea ulterioară, acesta NU va mai reveni.
ro.LegacyRemoveConfigCheck=Șterge și setările vechiului SampVoice (folderul Documente)
ro.SharedFilesNote=Fișierele partajate ale jocului (bass.dll, vorbisFile.dll, vorbisHooked.dll) sunt păstrate.
ro.DowngradePromptFmt=O versiune mai nouă (%1) este deja instalată. Instalați %2 oricum?
ro.UninstallKeepChangedWarn=Următorul fișier a fost modificat după instalare și a fost păstrat:%n%1
ro.UninstallRemoveIni=Doriți să ștergeți și setările din Documents\GTA San Andreas User Files\SAMP\sampvoice\?
ro.CloseGtaBeforeContinue=Închideți GTA San Andreas înainte de a continua.
ro.UninstallRestoreFailedWarn=Unele fișiere nu au putut fi restaurate sau eliminate (asigurați-vă că GTA San Andreas este închis).%nFișierele originale rămân salvate în:%n%1
ro.SummaryPlusRepairFmt=SampVoice Plus: verificare și reparare %1
ro.SummaryPlusUpdateFmt=SampVoice Plus: actualizare de la %1 la %2
ro.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: actualizare de la o versiune anterioară la %1
ro.SummaryLegacyBackup=SampVoice vechi: backup și înlocuire
ro.SummaryLegacyRemove=SampVoice vechi: eliminare definitivă
ro.SummaryLegacyRemoveCfg=Setări SampVoice vechi (Documente): ștergere
ro.WelcomeDownloadPre=Descarcă și actualizează mereu de pe
ro.FinishedDownloadPre=Versiunile noi se găsesc pe
ro.WelcomeDownloadLink=pagina oficială SampVoice Plus
ro.FinishedDownloadLink=pagina oficială SampVoice Plus

; --- Tiếng Việt (vi) ---
vi.MissingGtaExe=Không tìm thấy gta_sa.exe trong thư mục này. Vui lòng chọn thư mục GTA San Andreas 1.0.
vi.MissingSamp=Không tìm thấy SA-MP trong thư mục này (thiếu samp.dll hoặc bass.dll).%nHãy cài đặt SA-MP 0.3.7 (R1-R5) hoặc 0.3.DL trước rồi chạy lại trình cài đặt này.
vi.DownloadSampLink=Tải SA-MP (sa-mp.mp/downloads/)
vi.DownloadOmpLink=hoặc dùng open.mp Launcher
vi.DetectPageTitle=Kiểm tra môi trường
vi.DetectPageSubTitle=Kiểm tra phiên bản SA-MP, ASI loader và plugin hiện có
vi.SampDetectedFmt=Đã phát hiện SA-MP: %1 (tương thích)
vi.SampUnknownWarn=Không nhận diện được bản dựng SA-MP (%1). SampVoice Plus sẽ kiểm tra lại khi khởi chạy game.
vi.LoaderFoundFmt=Đã phát hiện ASI loader: %1 (không cần cài thêm).
vi.LoaderInstallUal=Chưa có ASI loader: sẽ cài đặt Ultimate ASI Loader (MIT) dạng dinput8.dll.
vi.LoaderConflictText=Phát hiện dinput8.dll bên thứ ba không rõ nguồn gốc và không có ASI loader khác. Hủy cài đặt để an toàn.
vi.PlusRepairFmt=SampVoice Plus %1 đã được cài đặt: các tệp sẽ được kiểm tra và sửa chữa.
vi.PlusUpdateFmt=Đang cập nhật SampVoice Plus từ %1 lên %2.
vi.PlusUpdateUnknownVerFmt=Đang cập nhật SampVoice Plus từ phiên bản cũ hơn lên %1.
vi.LegacyDetectedHeaderFmt=Phát hiện SampVoice cũ: %1
vi.LegacyManualTag=cài đặt thủ công
vi.LegacyBackupTitle=Sao lưu (khuyên dùng)
vi.LegacyBackupDesc=Các tệp SampVoice cũ được lưu lại. Khi gỡ SampVoice Plus, mọi thứ trở về như ban đầu.
vi.LegacyRemoveTitle=Gỡ bỏ SampVoice cũ
vi.LegacyRemoveDesc=Xóa vĩnh viễn SampVoice cũ. Khi gỡ SampVoice Plus sau này, bản cũ sẽ KHÔNG quay lại.
vi.LegacyRemoveConfigCheck=Xóa luôn cấu hình của SampVoice cũ (thư mục Documents)
vi.SharedFilesNote=Các tệp dùng chung của game (bass.dll, vorbisFile.dll, vorbisHooked.dll) luôn được giữ nguyên.
vi.DowngradePromptFmt=Phiên bản mới hơn (%1) đã được cài đặt. Vẫn cài đặt %2 chứ?
vi.UninstallKeepChangedWarn=Tệp sau đã bị chỉnh sửa sau khi cài đặt nên được giữ lại:%n%1
vi.UninstallRemoveIni=Bạn có muốn xóa luôn cấu hình tại Documents\GTA San Andreas User Files\SAMP\sampvoice\ không?
vi.CloseGtaBeforeContinue=Vui lòng đóng GTA San Andreas trước khi tiếp tục.
vi.UninstallRestoreFailedWarn=Một số tệp không thể khôi phục hoặc xóa (hãy chắc chắn đã đóng GTA San Andreas).%nCác tệp gốc của bạn vẫn được lưu tại:%n%1
vi.SummaryPlusRepairFmt=SampVoice Plus: kiểm tra và sửa chữa %1
vi.SummaryPlusUpdateFmt=SampVoice Plus: cập nhật từ %1 lên %2
vi.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: cập nhật từ phiên bản cũ hơn lên %1
vi.SummaryLegacyBackup=SampVoice cũ: sao lưu và thay thế
vi.SummaryLegacyRemove=SampVoice cũ: xóa vĩnh viễn
vi.SummaryLegacyRemoveCfg=Cấu hình SampVoice cũ (Documents): xóa
vi.WelcomeDownloadPre=Luôn tải và cập nhật tại
vi.FinishedDownloadPre=Các phiên bản mới có trên
vi.WelcomeDownloadLink=trang chính thức của SampVoice Plus
vi.FinishedDownloadLink=trang chính thức của SampVoice Plus

; --- Türkçe (tr) ---
tr.MissingGtaExe=Bu klasörde gta_sa.exe bulunamadı. Lütfen GTA San Andreas 1.0 klasörünü seçin.
tr.MissingSamp=Bu klasörde SA-MP bulunamadı (samp.dll / bass.dll eksik).%nÖnce SA-MP 0.3.7 (R1-R5) veya 0.3.DL kurun, ardından kurulumu yeniden çalıştırın.
tr.DownloadSampLink=SA-MP İndir (sa-mp.mp/downloads/)
tr.DownloadOmpLink=veya open.mp Launcher kullanın
tr.DetectPageTitle=Ortam Algılama
tr.DetectPageSubTitle=SA-MP sürümü, ASI yükleyici ve mevcut ses eklentileri denetleniyor
tr.SampDetectedFmt=SA-MP algılandı: %1 (uyumlu)
tr.SampUnknownWarn=Bilinmeyen SA-MP sürümü (%1). SampVoice Plus oyun açılışında doğrulayacaktır.
tr.LoaderFoundFmt=ASI yükleyici algılandı: %1 (ek yükleyici gerekmiyor).
tr.LoaderInstallUal=ASI yükleyici bulunamadı: Ultimate ASI Loader (MIT) dinput8.dll olarak kurulacak.
tr.LoaderConflictText=Başka ASI yükleyici olmadan bilinmeyen üçüncü taraf dinput8.dll bulundu. Modları korumak için kurulum iptal edildi.
tr.PlusRepairFmt=SampVoice Plus %1 zaten kurulu: dosyalar doğrulanıp onarılacak.
tr.PlusUpdateFmt=SampVoice Plus %1 sürümünden %2 sürümüne güncelleniyor.
tr.PlusUpdateUnknownVerFmt=SampVoice Plus önceki bir sürümden %1 sürümüne güncelleniyor.
tr.LegacyDetectedHeaderFmt=Eski SampVoice bulundu: %1
tr.LegacyManualTag=elle kurulmuş
tr.LegacyBackupTitle=Yedekle (önerilen)
tr.LegacyBackupDesc=Eski SampVoice dosyaları saklanır. SampVoice Plus kaldırıldığında her şey eski haline döner.
tr.LegacyRemoveTitle=Eski SampVoice'i kaldır
tr.LegacyRemoveDesc=Eski SampVoice'i kalıcı olarak siler. SampVoice Plus kaldırıldığında geri GELMEZ.
tr.LegacyRemoveConfigCheck=Eski SampVoice ayarlarını da sil (Belgeler klasörü)
tr.SharedFilesNote=Oyunun ortak dosyaları (bass.dll, vorbisFile.dll, vorbisHooked.dll) olduğu gibi korunur.
tr.DowngradePromptFmt=Daha yeni bir sürüm (%1) zaten kurulu. Yine de %2 kurulsun mu?
tr.UninstallKeepChangedWarn=Şu dosya kurulumdan sonra değiştirildiği için korundu:%n%1
tr.UninstallRemoveIni=Documents\GTA San Andreas User Files\SAMP\sampvoice\ içindeki ayarlar da silinsin mi?
tr.CloseGtaBeforeContinue=Lütfen devam etmeden önce GTA San Andreas'ı kapatın.
tr.UninstallRestoreFailedWarn=Bazı dosyalar geri yüklenemedi veya kaldırılamadı (GTA San Andreas'ın kapalı olduğundan emin olun).%nOrijinal dosyalarınız hâlâ şu klasörde saklanıyor:%n%1
tr.SummaryPlusRepairFmt=SampVoice Plus: %1 doğrula ve onar
tr.SummaryPlusUpdateFmt=SampVoice Plus: %1 -> %2 güncellemesi
tr.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: önceki sürümden %1 sürümüne güncelleme
tr.SummaryLegacyBackup=Eski SampVoice: yedekle ve değiştir
tr.SummaryLegacyRemove=Eski SampVoice: kalıcı olarak kaldır
tr.SummaryLegacyRemoveCfg=Eski SampVoice ayarları (Belgeler): sil
tr.WelcomeDownloadPre=Her zaman buradan indirin ve güncelleyin:
tr.FinishedDownloadPre=Yeni sürümler burada yayınlanır:
tr.WelcomeDownloadLink=resmi SampVoice Plus sayfası
tr.FinishedDownloadLink=resmi SampVoice Plus sayfası

; --- Български (bg) ---
bg.MissingGtaExe=gta_sa.exe не е намерен в тази папка. Моля, изберете папката на GTA San Andreas 1.0.
bg.MissingSamp=SA-MP не е намерен в тази папка (липсва samp.dll / bass.dll).%nИнсталирайте първо SA-MP 0.3.7 (R1-R5) или 0.3.DL и стартирайте инсталатора отново.
bg.DownloadSampLink=Изтегли SA-MP (sa-mp.mp/downloads/)
bg.DownloadOmpLink=или използвайте open.mp Launcher
bg.DetectPageTitle=Откриване на средата
bg.DetectPageSubTitle=Проверка на версията на SA-MP, ASI loader и съществуващи плъгини
bg.SampDetectedFmt=Открит SA-MP: %1 (съвместим)
bg.SampUnknownWarn=Неразпозната версия на SA-MP (%1). SampVoice Plus ще провери съвместимостта при стартиране.
bg.LoaderFoundFmt=Открит ASI loader: %1 (не е нужен нов loader).
bg.LoaderInstallUal=Няма ASI loader: Ultimate ASI Loader (MIT) ще бъде инсталиран като dinput8.dll.
bg.LoaderConflictText=Открит е непознат външен dinput8.dll без друг ASI loader. Инсталацията е отменена за безопасност.
bg.PlusRepairFmt=SampVoice Plus %1 вече е инсталиран: файловете ще бъдат проверени и поправени.
bg.PlusUpdateFmt=Актуализиране на SampVoice Plus от %1 до %2.
bg.PlusUpdateUnknownVerFmt=Актуализиране на SampVoice Plus от по-стара версия до %1.
bg.LegacyDetectedHeaderFmt=Открит е стар SampVoice: %1
bg.LegacyManualTag=ръчно инсталиран
bg.LegacyBackupTitle=Архивиране (препоръчително)
bg.LegacyBackupDesc=Файловете на стария SampVoice се запазват. При деинсталиране всичко се възстановява точно както е било.
bg.LegacyRemoveTitle=Деинсталиране на стария SampVoice
bg.LegacyRemoveDesc=Премахва стария SampVoice завинаги. При деинсталиране на SampVoice Plus той НЕ се връща.
bg.LegacyRemoveConfigCheck=Изтрий и настройките на стария SampVoice (папка Документи)
bg.SharedFilesNote=Споделените файлове на играта (bass.dll, vorbisFile.dll, vorbisHooked.dll) се запазват.
bg.DowngradePromptFmt=По-нова версия (%1) вече е инсталирана. Да се инсталира ли %2 въпреки това?
bg.UninstallKeepChangedWarn=Следният файл е променен след инсталацията и беше запазен:%n%1
bg.UninstallRemoveIni=Искате ли да премахнете и настройките в Documents\GTA San Andreas User Files\SAMP\sampvoice\?
bg.CloseGtaBeforeContinue=Моля, затворете GTA San Andreas, преди да продължите.
bg.UninstallRestoreFailedWarn=Някои файлове не можаха да бъдат възстановени или премахнати (уверете се, че GTA San Andreas е затворен).%nОригиналните ви файлове остават запазени в:%n%1
bg.SummaryPlusRepairFmt=SampVoice Plus: проверка и поправка на %1
bg.SummaryPlusUpdateFmt=SampVoice Plus: актуализация от %1 на %2
bg.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: актуализация от по-стара версия до %1
bg.SummaryLegacyBackup=Стар SampVoice: архивиране и замяна
bg.SummaryLegacyRemove=Стар SampVoice: пълно премахване
bg.SummaryLegacyRemoveCfg=Настройки на стария SampVoice (Документи): изтриване
bg.WelcomeDownloadPre=Винаги изтегляйте и обновявайте от
bg.FinishedDownloadPre=Новите версии се публикуват на
bg.WelcomeDownloadLink=официалната страница на SampVoice Plus
bg.FinishedDownloadLink=официалната страница на SampVoice Plus

; --- Русский (ru) ---
ru.MissingGtaExe=Файл gta_sa.exe не найден в этой папке. Укажите папку с GTA San Andreas 1.0.
ru.MissingSamp=В этой папке не найден SA-MP (отсутствует samp.dll или bass.dll).%nСначала установите SA-MP 0.3.7 (R1-R5) или 0.3.DL, а затем запустите установщик снова.
ru.DownloadSampLink=Скачать SA-MP (sa-mp.mp/downloads/)
ru.DownloadOmpLink=или использовать open.mp Launcher
ru.DetectPageTitle=Определение окружения
ru.DetectPageSubTitle=Проверка версии SA-MP, ASI loader и установленных плагинов
ru.SampDetectedFmt=Обнаружен SA-MP: %1 (совместим)
ru.SampUnknownWarn=Неизвестная сборка SA-MP (%1). SampVoice Plus проверит совместимость при запуске игры.
ru.LoaderFoundFmt=Найден ASI loader: %1 (установка загрузчика не требуется).
ru.LoaderInstallUal=ASI loader не найден: Ultimate ASI Loader (MIT) будет установлен как dinput8.dll.
ru.LoaderConflictText=Обнаружен сторонний неизвестный dinput8.dll без другого ASI loader. Установка отменена, чтобы не повредить ваши моды.
ru.PlusRepairFmt=SampVoice Plus %1 уже установлен: файлы будут проверены и восстановлены.
ru.PlusUpdateFmt=Обновление SampVoice Plus с %1 до %2.
ru.PlusUpdateUnknownVerFmt=Обновление SampVoice Plus с более ранней версии до %1.
ru.LegacyDetectedHeaderFmt=Найден старый SampVoice: %1
ru.LegacyManualTag=установлен вручную
ru.LegacyBackupTitle=Сделать резервную копию (рекомендуется)
ru.LegacyBackupDesc=Файлы старого SampVoice сохраняются. При удалении SampVoice Plus всё вернётся как было.
ru.LegacyRemoveTitle=Удалить старый SampVoice
ru.LegacyRemoveDesc=Удаляет старый SampVoice навсегда. После удаления SampVoice Plus он НЕ вернётся.
ru.LegacyRemoveConfigCheck=Также удалить настройки старого SampVoice (папка Документы)
ru.SharedFilesNote=Общие файлы игры (bass.dll, vorbisFile.dll, vorbisHooked.dll) сохраняются без изменений.
ru.DowngradePromptFmt=Более новая версия (%1) уже установлена. Всё равно установить %2?
ru.UninstallKeepChangedWarn=Этот файл был изменён после установки и сохранён без изменений:%n%1
ru.UninstallRemoveIni=Удалить также ваши настройки из Documents\GTA San Andreas User Files\SAMP\sampvoice\?
ru.CloseGtaBeforeContinue=Пожалуйста, закройте GTA San Andreas перед продолжением.
ru.UninstallRestoreFailedWarn=Некоторые файлы не удалось восстановить или удалить (убедитесь, что GTA San Andreas закрыт).%nИсходные файлы по-прежнему сохранены в:%n%1
ru.SummaryPlusRepairFmt=SampVoice Plus: проверка и восстановление %1
ru.SummaryPlusUpdateFmt=SampVoice Plus: обновление с %1 до %2
ru.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: обновление с более ранней версии до %1
ru.SummaryLegacyBackup=Старый SampVoice: резервная копия и замена
ru.SummaryLegacyRemove=Старый SampVoice: полное удаление
ru.SummaryLegacyRemoveCfg=Настройки старого SampVoice (Документы): удалить
ru.WelcomeDownloadPre=Всегда скачивайте и обновляйте с
ru.FinishedDownloadPre=Новые версии доступны на
ru.WelcomeDownloadLink=официальной страницы SampVoice Plus
ru.FinishedDownloadLink=официальной странице SampVoice Plus

; --- Українська (uk) ---
uk.MissingGtaExe=У цій папці не знайдено gta_sa.exe. Виберіть папку з GTA San Andreas 1.0.
uk.MissingSamp=У цій папці не знайдено SA-MP (відсутній samp.dll або bass.dll).%nСпочатку встановіть SA-MP 0.3.7 (R1-R5) або 0.3.DL, а потім запустіть інсталятор знову.
uk.DownloadSampLink=Завантажити SA-MP (sa-mp.mp/downloads/)
uk.DownloadOmpLink=або скористатися open.mp Launcher
uk.DetectPageTitle=Визначення середовища
uk.DetectPageSubTitle=Перевірка версії SA-MP, ASI loader та наявних плагінів
uk.SampDetectedFmt=Виявлено SA-MP: %1 (сумісний)
uk.SampUnknownWarn=Невідома збірка SA-MP (%1). SampVoice Plus перевірить сумісність при запуску гри.
uk.LoaderFoundFmt=Знайдено ASI loader: %1 (новий завантажувач не потрібен).
uk.LoaderInstallUal=ASI loader не знайдено: Ultimate ASI Loader (MIT) буде встановлено як dinput8.dll.
uk.LoaderConflictText=Знайдено невідомий сторонній dinput8.dll без іншого ASI loader. Встановлення скасовано для безпеки модів.
uk.PlusRepairFmt=SampVoice Plus %1 вже встановлено: файли буде перевірено та відновлено.
uk.PlusUpdateFmt=Оновлення SampVoice Plus з %1 до %2.
uk.PlusUpdateUnknownVerFmt=Оновлення SampVoice Plus з попередньої версії до %1.
uk.LegacyDetectedHeaderFmt=Знайдено старий SampVoice: %1
uk.LegacyManualTag=встановлено вручну
uk.LegacyBackupTitle=Зробити резервну копію (рекомендовано)
uk.LegacyBackupDesc=Файли старого SampVoice зберігаються. Після видалення SampVoice Plus усе повернеться як було.
uk.LegacyRemoveTitle=Видалити старий SampVoice
uk.LegacyRemoveDesc=Видаляє старий SampVoice назавжди. Після видалення SampVoice Plus він НЕ повернеться.
uk.LegacyRemoveConfigCheck=Також видалити налаштування старого SampVoice (папка Документи)
uk.SharedFilesNote=Спільні файли гри (bass.dll, vorbisFile.dll, vorbisHooked.dll) залишаються без змін.
uk.DowngradePromptFmt=Новіша версія (%1) вже встановлена. Усе одно встановити %2?
uk.UninstallKeepChangedWarn=Цей файл був змінений після встановлення і залишений без змін:%n%1
uk.UninstallRemoveIni=Видалити також ваші налаштування в Documents\GTA San Andreas User Files\SAMP\sampvoice\?
uk.CloseGtaBeforeContinue=Будь ласка, закрийте GTA San Andreas перед продовженням.
uk.UninstallRestoreFailedWarn=Деякі файли не вдалося відновити або видалити (переконайтеся, що GTA San Andreas закрито).%nВаші початкові файли досі збережені в:%n%1
uk.SummaryPlusRepairFmt=SampVoice Plus: перевірка та відновлення %1
uk.SummaryPlusUpdateFmt=SampVoice Plus: оновлення з %1 до %2
uk.SummaryPlusUpdateUnknownVerFmt=SampVoice Plus: оновлення з попередньої версії до %1
uk.SummaryLegacyBackup=Старий SampVoice: резервна копія та заміна
uk.SummaryLegacyRemove=Старий SampVoice: повне видалення
uk.SummaryLegacyRemoveCfg=Налаштування старого SampVoice (Документи): видалити
uk.WelcomeDownloadPre=Завжди завантажуйте та оновлюйте з
uk.FinishedDownloadPre=Нові версії публікуються на
uk.WelcomeDownloadLink=офіційної сторінки SampVoice Plus
uk.FinishedDownloadLink=офіційній сторінці SampVoice Plus

[Messages]
; --- English (en) ---
en.WelcomeLabel2=This will install [name/ver] into the GTA San Andreas folder.%n%nSA-MP 0.3.7 (R1-R5) or 0.3.DL must already be installed.%n%nClose SA-MP and GTA San Andreas before continuing.
en.SelectDirDesc=Where is GTA San Andreas (SA-MP) installed?
en.SelectDirLabel3=Setup will install [name] into the following GTA San Andreas folder (where gta_sa.exe is located).
en.SelectDirBrowseLabel=To continue, click Next. If GTA San Andreas is in another folder, click Browse.
en.FinishedLabelNoIcons=[name] has been installed into the GTA San Andreas folder.%n%nJoin a voice server and press F11 (default key) in-game to open the SampVoice Plus menu.
en.FinishedLabel=[name] has been installed into the GTA San Andreas folder.%n%nJoin a voice server and press F11 (default key) in-game to open the SampVoice Plus menu.

; --- Bahasa Indonesia (id) ---
id.WelcomeLabel2=Ini akan memasang [name/ver] ke folder GTA San Andreas.%n%nSA-MP 0.3.7 (R1-R5) atau 0.3.DL harus sudah terpasang.%n%nTutup SA-MP dan GTA San Andreas sebelum melanjutkan.
id.SelectDirDesc=Di mana GTA San Andreas (SA-MP) terpasang?
id.SelectDirLabel3=Pemasang akan memasang [name] ke folder GTA San Andreas berikut (tempat gta_sa.exe berada).
id.SelectDirBrowseLabel=Untuk melanjutkan, klik Maju. Jika GTA San Andreas berada di folder lain, klik Cari.
id.FinishedLabelNoIcons=[name] telah dipasang ke folder GTA San Andreas.%n%nMasuk ke server bersuara dan tekan F11 (tombol bawaan) dalam game untuk membuka menu SampVoice Plus.
id.FinishedLabel=[name] telah dipasang ke folder GTA San Andreas.%n%nMasuk ke server bersuara dan tekan F11 (tombol bawaan) dalam game untuk membuka menu SampVoice Plus.

; --- Deutsch (de) ---
de.WelcomeLabel2=Dies installiert [name/ver] in den GTA San Andreas Ordner.%n%nSA-MP 0.3.7 (R1-R5) oder 0.3.DL muss bereits installiert sein.%n%nSchließen Sie SA-MP und GTA San Andreas, bevor Sie fortfahren.
de.SelectDirDesc=Wo ist GTA San Andreas (SA-MP) installiert?
de.SelectDirLabel3=Das Setup installiert [name] in den folgenden GTA San Andreas Ordner (in dem sich gta_sa.exe befindet).
de.SelectDirBrowseLabel=Klicken Sie zum Fortfahren auf Weiter. Wenn sich GTA San Andreas in einem anderen Ordner befindet, klicken Sie auf Durchsuchen.
de.FinishedLabelNoIcons=[name] wurde im GTA San Andreas Ordner installiert.%n%nTreten Sie einem Sprachserver bei und drücken Sie im Spiel F11 (Standardtaste), um das SampVoice Plus Menü zu öffnen.
de.FinishedLabel=[name] wurde im GTA San Andreas Ordner installiert.%n%nTreten Sie einem Sprachserver bei und drücken Sie im Spiel F11 (Standardtaste), um das SampVoice Plus Menü zu öffnen.

; --- Español (es) ---
es.WelcomeLabel2=Esto instalará [name/ver] en la carpeta de GTA San Andreas.%n%nDebe tener instalado SA-MP 0.3.7 (R1-R5) o 0.3.DL.%n%nCierre SA-MP y GTA San Andreas antes de continuar.
es.SelectDirDesc=¿Dónde está instalado GTA San Andreas (SA-MP)?
es.SelectDirLabel3=El instalador instalará [name] en la siguiente carpeta de GTA San Andreas (donde se encuentra gta_sa.exe).
es.SelectDirBrowseLabel=Para continuar, haga clic en Siguiente. Si GTA San Andreas está en otra carpeta, haga clic en Examinar.
es.FinishedLabelNoIcons=[name] se ha instalado en la carpeta de GTA San Andreas.%n%nEntre en un servidor con voz y pulse F11 (tecla predeterminada) en el juego para abrir el menú de SampVoice Plus.
es.FinishedLabel=[name] se ha instalado en la carpeta de GTA San Andreas.%n%nEntre en un servidor con voz y pulse F11 (tecla predeterminada) en el juego para abrir el menú de SampVoice Plus.

; --- Français (fr) ---
fr.WelcomeLabel2=Cela va installer [name/ver] dans le dossier GTA San Andreas.%n%nSA-MP 0.3.7 (R1-R5) ou 0.3.DL doit déjà être installé.%n%nFermez SA-MP et GTA San Andreas avant de continuer.
fr.SelectDirDesc=Où est installé GTA San Andreas (SA-MP) ?
fr.SelectDirLabel3=L'assistant va installer [name] dans le dossier GTA San Andreas suivant (où se trouve gta_sa.exe).
fr.SelectDirBrowseLabel=Pour continuer, cliquez sur Suivant. Si GTA San Andreas se trouve dans un autre dossier, cliquez sur Parcourir.
fr.FinishedLabelNoIcons=[name] a été installé dans le dossier GTA San Andreas.%n%nRejoignez un serveur vocal et appuyez sur F11 (touche par défaut) en jeu pour ouvrir le menu SampVoice Plus.
fr.FinishedLabel=[name] a été installé dans le dossier GTA San Andreas.%n%nRejoignez un serveur vocal et appuyez sur F11 (touche par défaut) en jeu pour ouvrir le menu SampVoice Plus.

; --- Italiano (it) ---
it.WelcomeLabel2=Questo installerà [name/ver] nella cartella di GTA San Andreas.%n%nSA-MP 0.3.7 (R1-R5) o 0.3.DL deve essere già installato.%n%nChiudi SA-MP e GTA San Andreas prima di continuare.
it.SelectDirDesc=Dove è installato GTA San Andreas (SA-MP)?
it.SelectDirLabel3=Il programma installerà [name] nella seguente cartella di GTA San Andreas (dove si trova gta_sa.exe).
it.SelectDirBrowseLabel=Per continuare, fai clic su Avanti. Se GTA San Andreas si trova in un'altra cartella, fai clic su Sfoglia.
it.FinishedLabelNoIcons=[name] è stato installato nella cartella di GTA San Andreas.%n%nEntra in un server con voce e premi F11 (tasto predefinito) in gioco per aprire il menu di SampVoice Plus.
it.FinishedLabel=[name] è stato installato nella cartella di GTA San Andreas.%n%nEntra in un server con voce e premi F11 (tasto predefinito) in gioco per aprire il menu di SampVoice Plus.

; --- Magyar (hu) ---
hu.WelcomeLabel2=Ez telepíti a [name/ver] bővítményt a GTA San Andreas mappába.%n%nA SA-MP 0.3.7 (R1-R5) vagy 0.3.DL verziójának már telepítve kell lennie.%n%nZárd be a SA-MP klienst és a GTA San Andreas játékot a folytatás előtt.
hu.SelectDirDesc=Hol található a GTA San Andreas (SA-MP)?
hu.SelectDirLabel3=A telepítő a következő GTA San Andreas mappába telepíti a [name] fájljait (ahol a gta_sa.exe található).
hu.SelectDirBrowseLabel=A folytatáshoz kattints a Tovább gombra. Ha a GTA San Andreas másik mappában van, kattints a Tallózás gombra.
hu.FinishedLabelNoIcons=A [name] telepítve lett a GTA San Andreas mappába.%n%nCsatlakozz egy hangos szerverre, és nyomd meg az F11-et (alapértelmezett gomb) a játékban a SampVoice Plus menü megnyitásához.
hu.FinishedLabel=A [name] telepítve lett a GTA San Andreas mappába.%n%nCsatlakozz egy hangos szerverre, és nyomd meg az F11-et (alapértelmezett gomb) a játékban a SampVoice Plus menü megnyitásához.

; --- Polski (pl) ---
pl.WelcomeLabel2=Spowoduje to instalację [name/ver] w folderze GTA San Andreas.%n%nWymagany jest zainstalowany SA-MP 0.3.7 (R1-R5) lub 0.3.DL.%n%nZamknij SA-MP oraz GTA San Andreas przed kontynuowaniem.
pl.SelectDirDesc=Gdzie jest zainstalowane GTA San Andreas (SA-MP)?
pl.SelectDirLabel3=Instalator zainstaluje [name] w poniższym folderze GTA San Andreas (tam, gdzie znajduje się plik gta_sa.exe).
pl.SelectDirBrowseLabel=Aby kontynuować, kliknij Dalej. Jeśli GTA San Andreas znajduje się w innym folderze, kliknij Przeglądaj.
pl.FinishedLabelNoIcons=Zainstalowano [name] w folderze GTA San Andreas.%n%nWejdź na serwer głosowy i naciśnij F11 (domyślny klawisz) w grze, aby otworzyć menu SampVoice Plus.
pl.FinishedLabel=Zainstalowano [name] w folderze GTA San Andreas.%n%nWejdź na serwer głosowy i naciśnij F11 (domyślny klawisz) w grze, aby otworzyć menu SampVoice Plus.

; --- Português (pt) - Inclui revisão formal R56-10 ("pra" -> "para") ---
pt.WelcomeLabel2=Isto instalará o [name/ver] na pasta do GTA San Andreas.%n%nÉ preciso ter o SA-MP 0.3.7 (R1-R5) ou 0.3.DL instalado.%n%nFeche o SA-MP e o GTA San Andreas antes de continuar.
pt.SelectDirDesc=Onde o GTA San Andreas (SA-MP) está instalado?
pt.SelectDirLabel3=O instalador instalará o [name] na seguinte pasta do GTA San Andreas (onde está o gta_sa.exe).
pt.SelectDirBrowseLabel=Para continuar, clique em Avançar. Se o GTA San Andreas estiver em outra pasta, clique em Procurar.
pt.FinishedLabelNoIcons=O [name] foi instalado na pasta do GTA San Andreas.%n%nEntre num servidor com voz e aperte F11 (tecla padrão) no jogo para abrir o menu do SampVoice Plus.
pt.FinishedLabel=O [name] foi instalado na pasta do GTA San Andreas.%n%nEntre num servidor com voz e aperte F11 (tecla padrão) no jogo para abrir o menu do SampVoice Plus.
pt.SetupAppRunningError=O instalador detectou que o %1 está atualmente em execução.%n%nPor favor feche todas as instâncias dele agora, então clique em OK para continuar ou em Cancelar para sair.
pt.UninstallAppRunningError=O Desinstalador detectou que o %1 está atualmente em execução.%n%nPor favor feche todas as instâncias dele agora, então clique em OK para continuar ou em Cancelar para sair.
pt.PrivilegesRequiredOverrideText1=O %1 pode ser instalado para todos os usuários (requer privilégios administrativos) ou só para você.
pt.PrivilegesRequiredOverrideText2=O %1 pode ser instalado só para você ou para todos os usuários (requer privilégios administrativos).
pt.PrivilegesRequiredOverrideAllUsers=Instalar para &todos os usuários
pt.PrivilegesRequiredOverrideAllUsersRecommended=Instalar para &todos os usuários (recomendado)
pt.PrivilegesRequiredOverrideCurrentUser=Instalar só &para mim
pt.PrivilegesRequiredOverrideCurrentUserRecommended=Instalar só &para mim (recomendado)
pt.ExitSetupMessage=A Instalação não está completa. Se você sair agora o programa não será instalado.%n%nVocê pode executar o instalador novamente outra hora para completar a instalação.%n%nSair do instalador?
pt.ButtonYesToAll=Sim para &Todos
pt.ButtonNoToAll=Nã&o para Todos
pt.SelectLanguageLabel=Selecione o idioma para usar durante a instalação:
pt.ClickNext=Clique em Avançar para continuar ou em Cancelar para sair do instalador.
pt.PasswordLabel3=Por favor forneça a senha, então clique em Avançar para continuar. As senhas são caso-sensitivo.
pt.InfoBeforeClickLabel=Quando você estiver pronto para continuar com o instalador, clique em Avançar.
pt.InfoAfterClickLabel=Quando você estiver pronto para continuar com o instalador, clique em Avançar.
pt.DiskSpaceWarning=O instalador requer pelo menos %1 KB de espaço livre para instalar mas o drive selecionado só tem %2 KB disponíveis.%n%nVocê quer continuar de qualquer maneira?
pt.SelectComponentsLabel2=Selecione os componentes que você quer instalar; desmarque os componentes que você não quer instalar. Clique em Avançar quando você estiver pronto para continuar.
pt.SelectStartMenuFolderBrowseLabel=Para continuar clique em Avançar. Se você gostaria de selecionar uma pasta diferente, clique em Procurar.
pt.WizardReady=Pronto para Instalar
pt.ReadyLabel1=O instalador está agora pronto para começar a instalar o [name] no seu computador.
pt.ReadyLabel2a=Clique em Instalar para continuar com a instalação ou clique em Voltar se você quer revisar ou mudar quaisquer configurações.
pt.ReadyLabel2b=Clique em Instalar para continuar com a instalação.
pt.WizardPreparing=Preparando para Instalar
pt.PreparingDesc=O instalador está se preparando para instalar o [name] no seu computador.
pt.PreviousInstallNotCompleted=A instalação/remoção de um programa anterior não foi completada. Você precisará reiniciar o computador para completar essa instalação.%n%nApós reiniciar seu computador execute o instalador novamente para completar a instalação do [name].
pt.CannotContinue=O instalador não pode continuar. Por favor clique em Cancelar para sair.
pt.ClickFinish=Clique em Concluir para sair do Instalador.
pt.FinishedRestartLabel=Para completar a instalação do [name], o instalador deve reiniciar seu computador. Você gostaria de reiniciar agora?
pt.FinishedRestartMessage=Para completar a instalação do [name], o instalador deve reiniciar seu computador.%n%nVocê gostaria de reiniciar agora?
pt.UninstalledAndNeedsRestart=Para completar a desinstalação do %1, seu computador deve ser reiniciado.%n%nVocê gostaria de reiniciar agora?

; --- Română (ro) ---
ro.WelcomeLabel2=Acest program va instala [name/ver] în folderul GTA San Andreas.%n%nTrebuie să ai deja instalat SA-MP 0.3.7 (R1-R5) sau 0.3.DL.%n%nÎnchide SA-MP și GTA San Andreas înainte de a continua.
ro.SelectDirDesc=Unde este instalat GTA San Andreas (SA-MP)?
ro.SelectDirLabel3=Instalatorul va instala [name] în următorul folder GTA San Andreas (unde se află gta_sa.exe).
ro.SelectDirBrowseLabel=Pentru a continua, clichează pe Continuă. Dacă GTA San Andreas se află în alt folder, clichează pe Explorează.
ro.FinishedLabelNoIcons=[name] a fost instalat în folderul GTA San Andreas.%n%nIntră pe un server cu voce și apasă F11 (tasta implicită) în joc pentru a deschide meniul SampVoice Plus.
ro.FinishedLabel=[name] a fost instalat în folderul GTA San Andreas.%n%nIntră pe un server cu voce și apasă F11 (tasta implicită) în joc pentru a deschide meniul SampVoice Plus.

; --- Tiếng Việt (vi) ---
vi.WelcomeLabel2=Chương trình sẽ cài đặt [name/ver] vào thư mục GTA San Andreas.%n%nBạn cần cài sẵn SA-MP 0.3.7 (R1-R5) hoặc 0.3.DL.%n%nHãy đóng SA-MP và GTA San Andreas trước khi tiếp tục.
vi.SelectDirDesc=GTA San Andreas (SA-MP) được cài ở đâu?
vi.SelectDirLabel3=Trình cài đặt sẽ cài [name] vào thư mục GTA San Andreas sau (nơi chứa gta_sa.exe).
vi.SelectDirBrowseLabel=Nhấn 'Tiếp theo' để tiếp tục. Nếu GTA San Andreas ở thư mục khác, hãy nhấn 'Duyệt'.
vi.FinishedLabelNoIcons=[name] đã được cài vào thư mục GTA San Andreas.%n%nVào máy chủ có thoại và nhấn F11 (phím mặc định) trong game để mở menu SampVoice Plus.
vi.FinishedLabel=[name] đã được cài vào thư mục GTA San Andreas.%n%nVào máy chủ có thoại và nhấn F11 (phím mặc định) trong game để mở menu SampVoice Plus.

; --- Türkçe (tr) ---
tr.WelcomeLabel2=Bu kurulum [name/ver] eklentisini GTA San Andreas klasörüne yükleyecektir.%n%nSA-MP 0.3.7 (R1-R5) veya 0.3.DL kurulu olmalıdır.%n%nDevam etmeden önce SA-MP ve GTA San Andreas'ı kapatın.
tr.SelectDirDesc=GTA San Andreas (SA-MP) nereye kurulu?
tr.SelectDirLabel3=Kurulum [name] eklentisini aşağıdaki GTA San Andreas klasörüne (gta_sa.exe dosyasının bulunduğu yere) yükleyecektir.
tr.SelectDirBrowseLabel=Devam etmek için Sonraki'ne tıklayın. GTA San Andreas başka bir klasördeyse Göz at'a tıklayın.
tr.FinishedLabelNoIcons=[name], GTA San Andreas klasörüne kuruldu.%n%nSesli bir sunucuya girin ve SampVoice Plus menüsünü açmak için oyunda F11'e (varsayılan tuş) basın.
tr.FinishedLabel=[name], GTA San Andreas klasörüne kuruldu.%n%nSesli bir sunucuya girin ve SampVoice Plus menüsünü açmak için oyunda F11'e (varsayılan tuş) basın.

; --- Български (bg) ---
bg.WelcomeLabel2=Това ще инсталира [name/ver] в папката на GTA San Andreas.%n%nТрябва да имате инсталиран SA-MP 0.3.7 (R1-R5) или 0.3.DL.%n%nЗатворете SA-MP и GTA San Andreas, преди да продължите.
bg.SelectDirDesc=Къде е инсталиран GTA San Andreas (SA-MP)?
bg.SelectDirLabel3=Инсталаторът ще постави [name] в следната папка на GTA San Andreas (където се намира gta_sa.exe).
bg.SelectDirBrowseLabel=Натиснете „Напред“, за да продължите. Ако GTA San Andreas се намира в друга папка, натиснете „Преглед“.
bg.FinishedLabelNoIcons=[name] беше инсталиран в папката на GTA San Andreas.%n%nВлезте в сървър с глас и натиснете F11 (клавиш по подразбиране) в играта, за да отворите менюто на SampVoice Plus.
bg.FinishedLabel=[name] беше инсталиран в папката на GTA San Andreas.%n%nВлезте в сървър с глас и натиснете F11 (клавиш по подразбиране) в играта, за да отворите менюто на SampVoice Plus.

; --- Русский (ru) ---
ru.WelcomeLabel2=Установит [name/ver] в папку GTA San Andreas.%n%nДолжен быть установлен SA-MP 0.3.7 (R1-R5) или 0.3.DL.%n%nЗакройте SA-MP и GTA San Andreas перед продолжением.
ru.SelectDirDesc=Где установлена игра GTA San Andreas (SA-MP)?
ru.SelectDirLabel3=Установщик скопирует [name] в следующую папку GTA San Andreas (где расположен gta_sa.exe).
ru.SelectDirBrowseLabel=Нажмите «Далее», чтобы продолжить. Если GTA San Andreas находится в другой папке, нажмите «Обзор».
ru.FinishedLabelNoIcons=[name] установлен в папку GTA San Andreas.%n%nЗайдите на сервер с голосом и нажмите F11 (клавиша по умолчанию) в игре, чтобы открыть меню SampVoice Plus.
ru.FinishedLabel=[name] установлен в папку GTA San Andreas.%n%nЗайдите на сервер с голосом и нажмите F11 (клавиша по умолчанию) в игре, чтобы открыть меню SampVoice Plus.

; --- Українська (uk) ---
uk.WelcomeLabel2=Встановить [name/ver] до теки GTA San Andreas.%n%nМає бути встановлений SA-MP 0.3.7 (R1-R5) або 0.3.DL.%n%nЗакрийте SA-MP і GTA San Andreas перед продовженням.
uk.SelectDirDesc=Де встановлено GTA San Andreas (SA-MP)?
uk.SelectDirLabel3=Інсталятор встановить [name] у зазначену теку GTA San Andreas (де розташований gta_sa.exe).
uk.SelectDirBrowseLabel=Натисніть «Далі», щоб продовжити. Якщо GTA San Andreas міститься в іншій теці, натисніть «Огляд».
uk.FinishedLabelNoIcons=[name] встановлено до теки GTA San Andreas.%n%nЗайдіть на сервер із голосом і натисніть F11 (типова клавіша) у грі, щоб відкрити меню SampVoice Plus.
uk.FinishedLabel=[name] встановлено до теки GTA San Andreas.%n%nЗайдіть на сервер із голосом і натисніть F11 (типова клавіша) у грі, щоб відкрити меню SampVoice Plus.

[Files]
; Destruction/restoration is handled deterministically via manifest.ini hash checks in [Code]
Source: "{#SampVoiceAsiPath}"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "{#ThirdPartyNoticesPath}"; DestDir: "{app}\sampvoice-plus"; Flags: ignoreversion uninsneveruninstall
Source: "{#UalDllPath}"; DestDir: "{app}"; Check: ShouldInstallUal; Flags: ignoreversion uninsneveruninstall

[InstallDelete]
Type: files; Name: "{app}\sampvoice-plus\Uninstall SampVoice Plus.lnk"

[Icons]
Name: "{app}\Uninstall SampVoice Plus"; Filename: "{uninstallexe}"; WorkingDir: "{app}\sampvoice-plus"

[Code]
var
  DetectPage: TWizardPage;
  SampStatusLabel: TNewStaticText;
  LoaderStatusLabel: TNewStaticText;
  PlusStatusLabel: TNewStaticText;
  LegacyHeaderLabel: TNewStaticText;
  LegacyBackupRadio: TNewRadioButton;
  LegacyBackupDescLabel: TNewStaticText;
  LegacyRemoveRadio: TNewRadioButton;
  LegacyRemoveDescLabel: TNewStaticText;
  LegacyConfigCheck: TNewCheckBox;
  SafeNoteLabel: TNewStaticText;
  LinkSampLabel: TNewStaticText;
  LinkOmpLabel: TNewStaticText;
  WelcomeDownloadPreLabel: TNewStaticText;
  WelcomeDownloadLinkLabel: TNewStaticText;
  FinishedDownloadPreLabel: TNewStaticText;
  FinishedDownloadLinkLabel: TNewStaticText;

  DetectedSampName: string;
  DetectedSampKnown: Boolean;
  ExistingLoaderDesc: string;
  NeedsInstallUal: Boolean;
  PrevInstalledUal: Boolean;
  HasDinputConflict: Boolean;
  HasLegacyClient: Boolean;
  HasLegacyConfig: Boolean;
  LegacyRegistryFound: Boolean;
  LegacyUninstallerPath: string;
  LegacyDisplayName: string;
  LegacyDisplayVersion: string;
  InstalledPlusVersion: string;
  DowngradeConfirmedVersion: string;

function GetInitialGtaPath(Param: string): string;
var
  ExePath: string;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\SAMP', 'gta_sa_exe', ExePath) then
  begin
    Result := RemoveBackslash(ExtractFileDir(ExePath));
    if FileExists(Result + '\gta_sa.exe') then
      Exit;
  end;
  Result := ExpandConstant('{commonpf32}\Rockstar Games\GTA San Andreas');
end;

function NormalizeDir(const S: string): string;
var
  Clean: string;
begin
  Clean := Trim(S);
  StringChangeEx(Clean, '/', '\', True);
  Result := RemoveBackslash(Clean);
end;

function ExtractExeFromCommand(const Cmd: string): string;
var
  S: string;
  QPos, SpacePos: Integer;
begin
  S := Trim(Cmd);
  if S = '' then
  begin
    Result := '';
    Exit;
  end;
  if S[1] = '"' then
  begin
    Delete(S, 1, 1);
    QPos := Pos('"', S);
    if QPos > 0 then
      Result := Copy(S, 1, QPos - 1)
    else
      Result := S;
  end
  else
  begin
    SpacePos := Pos(' ', S);
    if (SpacePos > 0) and (Pos('.exe', Lowercase(Copy(S, 1, SpacePos - 1))) > 0) then
      Result := Copy(S, 1, SpacePos - 1)
    else
      Result := S;
  end;
end;

function QueryUninstallKeyInRoot(const RootKey: Integer; const SubKey: string;
  var UninstPath, InstallLoc, DispName, DispVer: string): Boolean;
var
  RawUninst: string;
begin
  Result := RegKeyExists(RootKey, SubKey);
  if not Result then
    Exit;
  RawUninst := '';
  InstallLoc := '';
  DispName := '';
  DispVer := '';
  RegQueryStringValue(RootKey, SubKey, 'UninstallString', RawUninst);
  if RawUninst = '' then
    RegQueryStringValue(RootKey, SubKey, 'QuietUninstallString', RawUninst);
  RegQueryStringValue(RootKey, SubKey, 'InstallLocation', InstallLoc);
  RegQueryStringValue(RootKey, SubKey, 'DisplayName', DispName);
  RegQueryStringValue(RootKey, SubKey, 'DisplayVersion', DispVer);
  UninstPath := ExtractExeFromCommand(RawUninst);
end;

function FindAppInUninstallRegistry(const SubKey, MatchAppDir: string; const UninstInSubdir: Boolean;
  var OutUninstPath, OutDispName, OutDispVer: string): Boolean;
var
  Roots: array of Integer;
  I, NumRoots: Integer;
  UninstPath, InstallLoc, DispName, DispVer, CandidateDir, NormTarget: string;
begin
  Result := False;
  OutUninstPath := '';
  OutDispName := '';
  OutDispVer := '';
  NormTarget := NormalizeDir(MatchAppDir);

  if IsWin64 then
  begin
    NumRoots := 3;
    SetArrayLength(Roots, 3);
    Roots[0] := HKLM32;
    Roots[1] := HKLM64;
    Roots[2] := HKCU;
  end
  else
  begin
    NumRoots := 2;
    SetArrayLength(Roots, 2);
    Roots[0] := HKLM32;
    Roots[1] := HKCU;
  end;

  for I := 0 to NumRoots - 1 do
  begin
    if QueryUninstallKeyInRoot(Roots[I], SubKey, UninstPath, InstallLoc, DispName, DispVer) then
    begin
      if NormTarget = '' then
      begin
        OutUninstPath := UninstPath;
        OutDispName := DispName;
        OutDispVer := DispVer;
        Result := True;
        Exit;
      end;

      if Trim(InstallLoc) <> '' then
        CandidateDir := NormalizeDir(InstallLoc)
      else if UninstPath <> '' then
      begin
        CandidateDir := NormalizeDir(ExtractFileDir(UninstPath));
        if UninstInSubdir then
          CandidateDir := NormalizeDir(ExtractFileDir(CandidateDir));
      end
      else
        CandidateDir := '';

      if (CandidateDir <> '') and (CompareText(CandidateDir, NormTarget) = 0) then
      begin
        OutUninstPath := UninstPath;
        OutDispName := DispName;
        OutDispVer := DispVer;
        Result := True;
        Exit;
      end;
    end;
  end;
end;

function IsLegacyRegistryKeyPresent(const AppDir: string): Boolean;
var
  DummyUninst, DummyName, DummyVer: string;
begin
  Result := FindAppInUninstallRegistry('{#LegacyAppKey}', AppDir, False, DummyUninst, DummyName, DummyVer);
end;

function ParseVersionPart(var S: string): Integer;
var
  DotPos, I: Integer;
  Token, Digits: string;
begin
  if S = '' then
  begin
    Result := 0;
    Exit;
  end;
  DotPos := Pos('.', S);
  if DotPos > 0 then
  begin
    Token := Copy(S, 1, DotPos - 1);
    Delete(S, 1, DotPos);
  end
  else
  begin
    Token := S;
    S := '';
  end;
  Digits := '';
  for I := 1 to Length(Token) do
  begin
    if (Token[I] >= '0') and (Token[I] <= '9') then
      Digits := Digits + Token[I]
    else
      Break;
  end;
  Result := StrToIntDef(Digits, 0);
end;

function CompareNumericVersions(const A, B: string): Integer;
var
  SA, SB: string;
  VA, VB, I: Integer;
begin
  SA := Trim(A);
  SB := Trim(B);
  for I := 1 to 4 do
  begin
    VA := ParseVersionPart(SA);
    VB := ParseVersionPart(SB);
    if VA < VB then
    begin
      Result := -1;
      Exit;
    end;
    if VA > VB then
    begin
      Result := 1;
      Exit;
    end;
  end;
  Result := 0;
end;

function NormalizeVersionDisplay(const V: string): string;
var
  S: string;
  I, Dots: Integer;
begin
  S := Trim(V);
  Dots := 0;
  for I := 1 to Length(S) do
  begin
    if S[I] = '.' then
      Dots := Dots + 1;
  end;
  if (Dots = 3) and (Length(S) > 2) and (Copy(S, Length(S) - 1, 2) = '.0') then
    Delete(S, Length(S) - 1, 2);
  Result := S;
end;

function GetLegacyConfigDir: string;
var
  OverrideDocs: string;
begin
  OverrideDocs := NormalizeDir(ExpandConstant('{param:TESTDOCS|}'));
  if (OverrideDocs <> '') and (Pos('\svplus-installer-tests\', Lowercase(OverrideDocs + '\')) > 0) then
    Result := OverrideDocs + '\GTA San Andreas User Files\sampvoice'
  else
    Result := ExpandConstant('{userdocs}\GTA San Andreas User Files\sampvoice');
end;

function IsGtaRunning: Boolean;
begin
  Result := FindWindowByClassName('Grand theft auto San Andreas') <> 0;
end;

function InitializeSetup(): Boolean;
var
  UninstPath, DispName, DispVer: string;
  Cmp: Integer;
begin
  Result := True;
  DowngradeConfirmedVersion := '';
  if FindAppInUninstallRegistry('{#PlusAppKey}', '', True, UninstPath, DispName, DispVer) and (DispVer <> '') then
  begin
    Cmp := CompareNumericVersions(DispVer, '{#MyAppVersion}');
    if Cmp > 0 then
    begin
      if WizardSilent then
      begin
        Log(Format('ERROR: Newer SampVoice Plus (%s) is already installed; silent downgrade to %s aborted.', [DispVer, '{#MyAppVersion}']));
        Result := False;
        Exit;
      end;
      if MsgBox(FmtMessage(CustomMessage('DowngradePromptFmt'), [DispVer, '{#MyAppVersion}']),
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then
      begin
        Result := False;
        Exit;
      end;
      DowngradeConfirmedVersion := NormalizeVersionDisplay(DispVer);
    end;
  end;
end;

function ReadLE32(const S: AnsiString; Offset: Integer): Cardinal;
var
  b0, b1, b2, b3: Cardinal;
begin
  if (Offset < 0) or (Offset + 4 > Length(S)) then
  begin
    Result := 0;
    Exit;
  end;
  b0 := Ord(S[Offset + 1]);
  b1 := Ord(S[Offset + 2]);
  b2 := Ord(S[Offset + 3]);
  b3 := Ord(S[Offset + 4]);
  Result := b0 or (b1 shl 8) or (b2 shl 16) or (b3 shl 24);
end;

function SvGetFileVersionInfoSizeW(lptstrFilename: string; var lpdwHandle: Cardinal): Cardinal;
  external 'GetFileVersionInfoSizeW@version.dll stdcall';
function SvGetFileVersionInfoW(lptstrFilename: string; dwHandle, dwLen: Cardinal; lpData: AnsiString): Boolean;
  external 'GetFileVersionInfoW@version.dll stdcall';
function SvVerQueryValueW(pBlock: AnsiString; lpSubBlock: string; var lplpBuffer: Cardinal; var puLen: Cardinal): Boolean;
  external 'VerQueryValueW@version.dll stdcall';
procedure SvRtlMoveMemoryDword(var Destination: Cardinal; SourcePtr: Cardinal; Length: Cardinal);
  external 'RtlMoveMemory@kernel32.dll stdcall';
function SvLstrcpynW(lpString1: string; lpString2Ptr: Cardinal; iMaxLength: Integer): Cardinal;
  external 'lstrcpynW@kernel32.dll stdcall';

function FileContainsAscii(const FilePath, Token: string): Boolean;
var
  Content: AnsiString;
begin
  Result := False;
  if FileExists(FilePath) and LoadStringFromFile(FilePath, Content) then
    Result := Pos(AnsiString(Token), Content) > 0;
end;

function QueryVerProductNameBlock(const InfoBuf: AnsiString; const SubBlock: string; var OutName: string): Boolean;
var
  ValPtr, ValLen: Cardinal;
  BufStr: string;
  CopyLen, NullPos: Integer;
begin
  Result := False;
  OutName := '';
  ValPtr := 0;
  ValLen := 0;
  if SvVerQueryValueW(InfoBuf, SubBlock, ValPtr, ValLen) and (ValPtr <> 0) and (ValLen > 0) then
  begin
    CopyLen := Integer(ValLen);
    if CopyLen > 512 then
      CopyLen := 512;
    SetLength(BufStr, CopyLen);
    SvLstrcpynW(BufStr, ValPtr, CopyLen);
    NullPos := Pos(#0, BufStr);
    if NullPos > 0 then
      BufStr := Copy(BufStr, 1, NullPos - 1);
    OutName := Trim(BufStr);
    Result := OutName <> '';
  end;
end;

function TryGetFileVersionProductName(const FilePath: string; var OutProductName: string): Boolean;
var
  DummyHandle, InfoSize, TransPtr, TransLen, LangCp, WLang, WCodePage: Cardinal;
  InfoBuf: AnsiString;
begin
  Result := False;
  OutProductName := '';
  if not FileExists(FilePath) then
    Exit;

  DummyHandle := 0;
  InfoSize := SvGetFileVersionInfoSizeW(FilePath, DummyHandle);
  if InfoSize = 0 then
    Exit;

  SetLength(InfoBuf, Integer(InfoSize));
  if not SvGetFileVersionInfoW(FilePath, 0, InfoSize, InfoBuf) then
    Exit;

  TransPtr := 0;
  TransLen := 0;
  if SvVerQueryValueW(InfoBuf, '\VarFileInfo\Translation', TransPtr, TransLen) and
     (TransPtr <> 0) and (TransLen >= 4) then
  begin
    LangCp := 0;
    SvRtlMoveMemoryDword(LangCp, TransPtr, 4);
    WLang := LangCp and $FFFF;
    WCodePage := (LangCp shr 16) and $FFFF;
    if QueryVerProductNameBlock(InfoBuf, Format('\StringFileInfo\%.4x%.4x\ProductName', [WLang, WCodePage]), OutProductName) then
    begin
      Result := True;
      Exit;
    end;
  end;

  if QueryVerProductNameBlock(InfoBuf, '\StringFileInfo\040904B0\ProductName', OutProductName) then
    Result := True;
end;

function IdentifySampBuild(const SampDllPath: string; var BuildName: string): Boolean;
var
  Buf: AnsiString;
  PeOff: Integer;
  Stamp, Ep: Cardinal;
begin
  Result := False;
  BuildName := 'Unknown';
  if not LoadStringFromFile(SampDllPath, Buf) then
    Exit;
  if Length(Buf) < $100 then
    Exit;
  if (Buf[1] <> 'M') or (Buf[2] <> 'Z') then
    Exit;

  PeOff := Integer(ReadLE32(Buf, $3C));
  if (PeOff <= 0) or (PeOff + $2C > Length(Buf)) then
    Exit;
  if (Buf[PeOff + 1] <> 'P') or (Buf[PeOff + 2] <> 'E') or
     (Ord(Buf[PeOff + 3]) <> 0) or (Ord(Buf[PeOff + 4]) <> 0) then
    Exit;

  Stamp := ReadLE32(Buf, PeOff + 8);
  Ep := ReadLE32(Buf, PeOff + $28);

  if (Stamp = $5542F47A) and (Ep = $31DF13) then begin BuildName := '0.3.7-R1'; Result := True; Exit; end;
  if (Stamp = $59C30C94) and (Ep = $3195DD) then begin BuildName := '0.3.7-R2'; Result := True; Exit; end;
  if (Stamp = $5C0B4243) and (Ep = $0CC4D0) then begin BuildName := '0.3.7-R3-1'; Result := True; Exit; end;
  if (Stamp = $5DD606CD) and (Ep = $0CBCB0) then begin BuildName := '0.3.7-R4'; Result := True; Exit; end;
  if (Stamp = $6094ACAB) and (Ep = $0CBCD0) then begin BuildName := '0.3.7-R4-2'; Result := True; Exit; end;
  if (Stamp = $6372C39E) and (Ep = $0CBC90) then begin BuildName := '0.3.7-R5-1 / R5-2-MP'; Result := True; Exit; end;
  if (Stamp = $5A6A3130) and (Ep = $0FDB60) then begin BuildName := '0.3.DL-R1'; Result := True; Exit; end;

  BuildName := Format('TS=0x%.8x, EP=0x%.8x', [Stamp, Ep]);
end;

function IsFileListedInManifest(const RelPath, SectionName, ManifestPath: string): Boolean;
var
  Count, I: Integer;
  Entry: string;
begin
  Result := False;
  if not FileExists(ManifestPath) then
    Exit;
  Count := GetIniInt('Counts', SectionName, 0, 0, 100000, ManifestPath);
  I := 0;
  while True do
  begin
    Entry := GetIniString(SectionName, Format('File%d', [I]), '', ManifestPath);
    if Entry = '' then
    begin
      if I >= Count then
        Break;
    end
    else if CompareText(Entry, RelPath) = 0 then
    begin
      Result := True;
      Exit;
    end;
    I := I + 1;
  end;
end;

function IsPlusClientAsi(const FilePath: string): Boolean;
var
  ProductName: string;
begin
  Result := False;
  if not FileExists(FilePath) then
    Exit;
  // Detect SampVoice Plus by exported marker bytes OR UTF-16 VERSIONINFO ProductName.
  if FileContainsAscii(FilePath, 'SampVoice Plus Client') then
  begin
    Result := True;
    Exit;
  end;
  if TryGetFileVersionProductName(FilePath, ProductName) and
     (CompareText(Trim(ProductName), 'SampVoice Plus') = 0) then
  begin
    Result := True;
  end;
end;

function HasLegacyScriptsAsi(const AppDir: string): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if not DirExists(AppDir + '\scripts') then
    Exit;
  if FindFirst(AppDir + '\scripts\sampvoice*.asi', FindRec) then
  begin
    try
      repeat
        if ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0) and
           (not IsPlusClientAsi(AppDir + '\scripts\' + FindRec.Name)) then
        begin
          Result := True;
          Exit;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

function IsUalProxyDll(const FilePath: string): Boolean;
begin
  Result := False;
  if not FileExists(FilePath) then
    Exit;
  if CompareText(GetSHA256OfFile(FilePath), '{#KnownUalSha256}') = 0 then
  begin
    Result := True;
    Exit;
  end;
  // R5-2-MP / DL-R1-2-MP winmm.dll is a masterlist fix, never an ASI loader
  Result := FileContainsAscii(FilePath, 'Ultimate ASI Loader');
end;

procedure InspectTargetDirectory(const AppDir: string);
var
  ManifestPath, ProxyName, CandidatePath, DummyUninst, DummyName, PlusVer: string;
  UalProxies: array of string;
  I, Cmp: Integer;
  HasManualLegacy: Boolean;
begin
  DetectedSampKnown := IdentifySampBuild(AppDir + '\samp.dll', DetectedSampName);

  ManifestPath := AppDir + '\sampvoice-plus\manifest.ini';
  PrevInstalledUal := IsFileListedInManifest('dinput8.dll', 'Created', ManifestPath);

  ExistingLoaderDesc := '';
  NeedsInstallUal := False;
  HasDinputConflict := False;

  if FileExists(AppDir + '\vorbisHooked.dll') and FileExists(AppDir + '\vorbisFile.dll') then
    ExistingLoaderDesc := 'vorbisFile.dll (Silent''s ASI Loader)'
  else if FileContainsAscii(AppDir + '\vorbisFile.dll', '.asi') then
    ExistingLoaderDesc := 'vorbisFile.dll (ASI Loader)'
  else
  begin
    SetArrayLength(UalProxies, 8);
    UalProxies[0] := 'dinput8.dll';
    UalProxies[1] := 'd3d9.dll';
    UalProxies[2] := 'd3d8.dll';
    UalProxies[3] := 'winmm.dll';
    UalProxies[4] := 'version.dll';
    UalProxies[5] := 'dsound.dll';
    UalProxies[6] := 'wininet.dll';
    UalProxies[7] := 'vorbisFile.dll';

    for I := 0 to GetArrayLength(UalProxies) - 1 do
    begin
      ProxyName := UalProxies[I];
      if (CompareText(ProxyName, 'dinput8.dll') = 0) and PrevInstalledUal then
        Continue;
      CandidatePath := AppDir + '\' + ProxyName;
      if IsUalProxyDll(CandidatePath) then
      begin
        ExistingLoaderDesc := ProxyName + ' (Ultimate ASI Loader)';
        Break;
      end;
    end;
  end;

  if ExistingLoaderDesc = '' then
  begin
    if PrevInstalledUal then
      NeedsInstallUal := True
    else if FileExists(AppDir + '\dinput8.dll') then
      HasDinputConflict := True
    else
      NeedsInstallUal := True;
  end;

  LegacyRegistryFound := FindAppInUninstallRegistry(
    '{#LegacyAppKey}', AppDir, False,
    LegacyUninstallerPath, LegacyDisplayName, LegacyDisplayVersion);

  HasManualLegacy := (FileExists(AppDir + '\sampvoice.asi') and
                      not IsPlusClientAsi(AppDir + '\sampvoice.asi')) or
                     HasLegacyScriptsAsi(AppDir) or
                     DirExists(AppDir + '\sampvoice') or
                     FileExists(AppDir + '\bass_fx.dll');

  HasLegacyClient := LegacyRegistryFound or HasManualLegacy;
  HasLegacyConfig := DirExists(GetLegacyConfigDir);

  InstalledPlusVersion := '';
  if FindAppInUninstallRegistry('{#PlusAppKey}', AppDir, True, DummyUninst, DummyName, PlusVer) and (PlusVer <> '') then
    InstalledPlusVersion := NormalizeVersionDisplay(PlusVer)
  else if FileExists(ManifestPath) and IsPlusClientAsi(AppDir + '\sampvoice.asi') then
  begin
    if GetVersionNumbersString(AppDir + '\sampvoice.asi', PlusVer) and (PlusVer <> '') then
      InstalledPlusVersion := NormalizeVersionDisplay(PlusVer)
    else
    begin
      InstalledPlusVersion := '0.0.0';
      Log('SV+ Installed Plus version unknown (no VERSIONINFO); treating as update');
    end;
  end;

  Log('SV+ Detect SA-MP: ' + DetectedSampName);
  if ExistingLoaderDesc <> '' then
    Log('SV+ ASI loader detected: ' + ExistingLoaderDesc)
  else if NeedsInstallUal then
    Log('SV+ No external ASI loader detected; will install Ultimate ASI Loader (dinput8.dll)')
  else if HasDinputConflict then
    Log('SV+ Unknown third-party dinput8.dll conflict detected');

  if InstalledPlusVersion <> '' then
  begin
    Cmp := CompareNumericVersions(InstalledPlusVersion, '{#MyAppVersion}');
    if Cmp = 0 then
      Log('SV+ Repair mode: SampVoice Plus ' + InstalledPlusVersion + ' already installed in ' + AppDir)
    else if Cmp < 0 then
    begin
      if InstalledPlusVersion = '0.0.0' then
        Log('SV+ Upgrade mode: earlier build (unknown version) -> {#MyAppVersion} in ' + AppDir)
      else
        Log('SV+ Upgrade mode: ' + InstalledPlusVersion + ' -> {#MyAppVersion} in ' + AppDir);
    end
    else
      Log(Format('SV+ Newer SampVoice Plus detected (%s > %s) in %s', [InstalledPlusVersion, '{#MyAppVersion}', AppDir]));
  end;

  if LegacyRegistryFound then
    Log('SV+ Legacy setup detected in registry: ' + Trim(LegacyDisplayName + ' ' + LegacyDisplayVersion) + ' (' + LegacyUninstallerPath + ')')
  else if HasManualLegacy then
    Log('SV+ Legacy SampVoice detected (manual installation)');
  if HasLegacyConfig then
    Log('SV+ Legacy Documents config detected: ' + GetLegacyConfigDir);
end;

function ShouldInstallUal: Boolean;
begin
  Result := NeedsInstallUal and not HasDinputConflict;
end;

procedure OpenUrlClick(Sender: TObject);
var
  ErrCode: Integer;
begin
  if Sender = LinkSampLabel then
    ShellExec('open', '{#SampDownloadURL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrCode)
  else if Sender = LinkOmpLabel then
    ShellExec('open', '{#OmpLauncherURL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrCode)
  else if (Sender = WelcomeDownloadLinkLabel) or (Sender = FinishedDownloadLinkLabel) then
    ShellExec('open', '{#MyDownloadURL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrCode);
end;

procedure ConfigureThemeLinkLabel(Lbl: TNewStaticText; const URLHint: string);
begin
  Lbl.Font.Style := [fsUnderline];
  Lbl.StyleElements := Lbl.StyleElements - [seFont];
  if IsDarkInstallMode then
    Lbl.Font.Color := StrToColor('#60CDFF')
  else
    Lbl.Font.Color := StrToColor('#0066CC');
  Lbl.Cursor := crHand;
  Lbl.OnClick := @OpenUrlClick;
  if URLHint <> '' then
  begin
    Lbl.Hint := URLHint;
    Lbl.ShowHint := True;
  end;
end;

procedure LayoutPageDownloadLink(RefLabel, PreLabel, LinkLabel: TNewStaticText);
var
  BaseTop, MaxW: Integer;
begin
  if (RefLabel = nil) or (PreLabel = nil) or (LinkLabel = nil) then
    Exit;
  RefLabel.AdjustHeight;
  BaseTop := RefLabel.Top + RefLabel.Height + ScaleY(14);
  MaxW := RefLabel.Width;

  PreLabel.WordWrap := False;
  PreLabel.AutoSize := True;
  PreLabel.Left := RefLabel.Left;
  PreLabel.Top := BaseTop;

  LinkLabel.WordWrap := False;
  LinkLabel.AutoSize := True;

  if PreLabel.Width + ScaleX(4) + LinkLabel.Width <= MaxW then
  begin
    LinkLabel.Left := PreLabel.Left + PreLabel.Width + ScaleX(4);
    LinkLabel.Top := BaseTop;
  end
  else
  begin
    if PreLabel.Width > MaxW then
    begin
      PreLabel.WordWrap := True;
      PreLabel.Width := MaxW;
      PreLabel.AdjustHeight;
    end;
    LinkLabel.Left := RefLabel.Left;
    LinkLabel.Top := PreLabel.Top + PreLabel.Height + ScaleY(2);
    if LinkLabel.Width > MaxW then
    begin
      LinkLabel.WordWrap := True;
      LinkLabel.Width := MaxW;
      LinkLabel.AdjustHeight;
    end;
  end;
end;

procedure SyncLegacyControlStates;
begin
  LegacyConfigCheck.Enabled := LegacyRemoveRadio.Checked;
  if LegacyBackupRadio.Checked then
    LegacyConfigCheck.Checked := False;
end;

procedure LegacyOptionClick(Sender: TObject);
begin
  if (Sender = LegacyBackupDescLabel) then
    LegacyBackupRadio.Checked := True
  else if (Sender = LegacyRemoveDescLabel) then
    LegacyRemoveRadio.Checked := True;
  SyncLegacyControlStates;
end;

procedure FitWrappedLabel(const Lbl: TNewStaticText; const LeftX, TopY, MaxWidth: Integer);
begin
  Lbl.Left := LeftX;
  Lbl.Top := TopY;
  Lbl.AutoSize := False;
  Lbl.Width := MaxWidth;
  Lbl.Height := ScaleY(16);
  Lbl.AutoSize := True;
  Lbl.Width := MaxWidth;
end;

procedure LayoutDetectPageControls;
var
  TopY, W, IndentX, IndentW: Integer;
begin
  W := DetectPage.SurfaceWidth;
  IndentX := ScaleX(20);
  IndentW := W - IndentX;
  TopY := ScaleY(4);

  FitWrappedLabel(SampStatusLabel, 0, TopY, W);
  TopY := TopY + SampStatusLabel.Height + ScaleY(8);

  FitWrappedLabel(LoaderStatusLabel, 0, TopY, W);
  TopY := TopY + LoaderStatusLabel.Height + ScaleY(8);

  if PlusStatusLabel.Visible then
  begin
    FitWrappedLabel(PlusStatusLabel, 0, TopY, W);
    TopY := TopY + PlusStatusLabel.Height + ScaleY(8);
  end;

  if HasLegacyClient then
  begin
    FitWrappedLabel(LegacyHeaderLabel, 0, TopY, W);
    TopY := TopY + LegacyHeaderLabel.Height + ScaleY(6);

    LegacyBackupRadio.Left := 0;
    LegacyBackupRadio.Top := TopY;
    LegacyBackupRadio.Width := W;
    TopY := TopY + ScaleY(18);

    FitWrappedLabel(LegacyBackupDescLabel, IndentX, TopY, IndentW);
    TopY := TopY + LegacyBackupDescLabel.Height + ScaleY(6);

    LegacyRemoveRadio.Left := 0;
    LegacyRemoveRadio.Top := TopY;
    LegacyRemoveRadio.Width := W;
    TopY := TopY + ScaleY(18);

    FitWrappedLabel(LegacyRemoveDescLabel, IndentX, TopY, IndentW);
    TopY := TopY + LegacyRemoveDescLabel.Height + ScaleY(4);

    if LegacyConfigCheck.Visible then
    begin
      LegacyConfigCheck.Left := IndentX;
      LegacyConfigCheck.Top := TopY;
      LegacyConfigCheck.Width := IndentW;
      TopY := TopY + ScaleY(20);
    end;

    TopY := TopY + ScaleY(6);
  end;

  FitWrappedLabel(SafeNoteLabel, 0, TopY, W);
  TopY := TopY + SafeNoteLabel.Height + ScaleY(10);

  LinkSampLabel.Left := 0;
  LinkSampLabel.Top := TopY;
  if LinkSampLabel.Width + ScaleX(18) + LinkOmpLabel.Width <= W then
  begin
    LinkOmpLabel.Left := LinkSampLabel.Width + ScaleX(18);
    LinkOmpLabel.Top := TopY;
  end
  else
  begin
    LinkOmpLabel.Left := 0;
    LinkOmpLabel.Top := TopY + LinkSampLabel.Height + ScaleY(4);
  end;
end;

procedure UpdateDetectPageControls;
var
  LegacyTag: string;
  Cmp: Integer;
begin
  if DetectedSampKnown then
    SampStatusLabel.Caption := FmtMessage(CustomMessage('SampDetectedFmt'), [DetectedSampName])
  else
    SampStatusLabel.Caption := FmtMessage(CustomMessage('SampUnknownWarn'), [DetectedSampName]);

  if HasDinputConflict then
    LoaderStatusLabel.Caption := CustomMessage('LoaderConflictText')
  else if ExistingLoaderDesc <> '' then
    LoaderStatusLabel.Caption := FmtMessage(CustomMessage('LoaderFoundFmt'), [ExistingLoaderDesc])
  else
    LoaderStatusLabel.Caption := CustomMessage('LoaderInstallUal');

  PlusStatusLabel.Visible := False;
  if InstalledPlusVersion <> '' then
  begin
    Cmp := CompareNumericVersions(InstalledPlusVersion, '{#MyAppVersion}');
    if Cmp = 0 then
    begin
      PlusStatusLabel.Caption := FmtMessage(CustomMessage('PlusRepairFmt'), [InstalledPlusVersion]);
      PlusStatusLabel.Visible := True;
    end
    else if Cmp < 0 then
    begin
      if InstalledPlusVersion = '0.0.0' then
        PlusStatusLabel.Caption := FmtMessage(CustomMessage('PlusUpdateUnknownVerFmt'), ['{#MyAppVersion}'])
      else
        PlusStatusLabel.Caption := FmtMessage(CustomMessage('PlusUpdateFmt'), [InstalledPlusVersion, '{#MyAppVersion}']);
      PlusStatusLabel.Visible := True;
    end;
  end;

  if HasLegacyClient then
  begin
    if LegacyRegistryFound then
      LegacyTag := Trim(LegacyDisplayName + ' ' + LegacyDisplayVersion)
    else
      LegacyTag := CustomMessage('LegacyManualTag');
    if LegacyTag = '' then
      LegacyTag := 'SampVoice';

    LegacyHeaderLabel.Caption := FmtMessage(CustomMessage('LegacyDetectedHeaderFmt'), [LegacyTag]);
    LegacyHeaderLabel.Visible := True;
    LegacyBackupRadio.Visible := True;
    LegacyBackupDescLabel.Visible := True;
    LegacyRemoveRadio.Visible := True;
    LegacyRemoveDescLabel.Visible := True;
    LegacyBackupRadio.Checked := True;
    LegacyRemoveRadio.Checked := False;
    LegacyConfigCheck.Visible := HasLegacyConfig;
    LegacyConfigCheck.Checked := False;
    SyncLegacyControlStates;
  end
  else
  begin
    LegacyHeaderLabel.Visible := False;
    LegacyBackupRadio.Visible := False;
    LegacyBackupDescLabel.Visible := False;
    LegacyRemoveRadio.Visible := False;
    LegacyRemoveDescLabel.Visible := False;
    LegacyConfigCheck.Visible := False;
  end;

  LayoutDetectPageControls;
end;

procedure InitializeWizard;
begin
  DetectPage := CreateCustomPage(
    wpSelectDir,
    CustomMessage('DetectPageTitle'),
    CustomMessage('DetectPageSubTitle')
  );

  SampStatusLabel := TNewStaticText.Create(DetectPage);
  SampStatusLabel.Parent := DetectPage.Surface;
  SampStatusLabel.Left := 0;
  SampStatusLabel.Width := DetectPage.SurfaceWidth;
  SampStatusLabel.WordWrap := True;
  SampStatusLabel.AutoSize := True;

  LoaderStatusLabel := TNewStaticText.Create(DetectPage);
  LoaderStatusLabel.Parent := DetectPage.Surface;
  LoaderStatusLabel.Left := 0;
  LoaderStatusLabel.Width := DetectPage.SurfaceWidth;
  LoaderStatusLabel.WordWrap := True;
  LoaderStatusLabel.AutoSize := True;

  PlusStatusLabel := TNewStaticText.Create(DetectPage);
  PlusStatusLabel.Parent := DetectPage.Surface;
  PlusStatusLabel.Left := 0;
  PlusStatusLabel.Width := DetectPage.SurfaceWidth;
  PlusStatusLabel.Font.Style := [fsBold];
  PlusStatusLabel.WordWrap := True;
  PlusStatusLabel.AutoSize := True;
  PlusStatusLabel.Visible := False;

  LegacyHeaderLabel := TNewStaticText.Create(DetectPage);
  LegacyHeaderLabel.Parent := DetectPage.Surface;
  LegacyHeaderLabel.Left := 0;
  LegacyHeaderLabel.Width := DetectPage.SurfaceWidth;
  LegacyHeaderLabel.Font.Style := [fsBold];
  LegacyHeaderLabel.WordWrap := True;
  LegacyHeaderLabel.AutoSize := True;
  LegacyHeaderLabel.Visible := False;

  LegacyBackupRadio := TNewRadioButton.Create(DetectPage);
  LegacyBackupRadio.Parent := DetectPage.Surface;
  LegacyBackupRadio.Left := 0;
  LegacyBackupRadio.Width := DetectPage.SurfaceWidth;
  LegacyBackupRadio.Height := ScaleY(17);
  LegacyBackupRadio.Caption := CustomMessage('LegacyBackupTitle');
  LegacyBackupRadio.Font.Style := [fsBold];
  LegacyBackupRadio.OnClick := @LegacyOptionClick;
  LegacyBackupRadio.Visible := False;

  LegacyBackupDescLabel := TNewStaticText.Create(DetectPage);
  LegacyBackupDescLabel.Parent := DetectPage.Surface;
  LegacyBackupDescLabel.Left := ScaleX(20);
  LegacyBackupDescLabel.Width := DetectPage.SurfaceWidth - ScaleX(20);
  LegacyBackupDescLabel.WordWrap := True;
  LegacyBackupDescLabel.AutoSize := True;
  LegacyBackupDescLabel.Caption := CustomMessage('LegacyBackupDesc');
  LegacyBackupDescLabel.OnClick := @LegacyOptionClick;
  LegacyBackupDescLabel.Visible := False;

  LegacyRemoveRadio := TNewRadioButton.Create(DetectPage);
  LegacyRemoveRadio.Parent := DetectPage.Surface;
  LegacyRemoveRadio.Left := 0;
  LegacyRemoveRadio.Width := DetectPage.SurfaceWidth;
  LegacyRemoveRadio.Height := ScaleY(17);
  LegacyRemoveRadio.Caption := CustomMessage('LegacyRemoveTitle');
  LegacyRemoveRadio.Font.Style := [fsBold];
  LegacyRemoveRadio.OnClick := @LegacyOptionClick;
  LegacyRemoveRadio.Visible := False;

  LegacyRemoveDescLabel := TNewStaticText.Create(DetectPage);
  LegacyRemoveDescLabel.Parent := DetectPage.Surface;
  LegacyRemoveDescLabel.Left := ScaleX(20);
  LegacyRemoveDescLabel.Width := DetectPage.SurfaceWidth - ScaleX(20);
  LegacyRemoveDescLabel.WordWrap := True;
  LegacyRemoveDescLabel.AutoSize := True;
  LegacyRemoveDescLabel.Caption := CustomMessage('LegacyRemoveDesc');
  LegacyRemoveDescLabel.OnClick := @LegacyOptionClick;
  LegacyRemoveDescLabel.Visible := False;

  LegacyConfigCheck := TNewCheckBox.Create(DetectPage);
  LegacyConfigCheck.Parent := DetectPage.Surface;
  LegacyConfigCheck.Left := ScaleX(20);
  LegacyConfigCheck.Width := DetectPage.SurfaceWidth - ScaleX(20);
  LegacyConfigCheck.Height := ScaleY(18);
  LegacyConfigCheck.Caption := CustomMessage('LegacyRemoveConfigCheck');
  LegacyConfigCheck.Visible := False;

  SafeNoteLabel := TNewStaticText.Create(DetectPage);
  SafeNoteLabel.Parent := DetectPage.Surface;
  SafeNoteLabel.Left := 0;
  SafeNoteLabel.Width := DetectPage.SurfaceWidth;
  SafeNoteLabel.WordWrap := True;
  SafeNoteLabel.AutoSize := True;
  SafeNoteLabel.Caption := CustomMessage('SharedFilesNote');

  LinkSampLabel := TNewStaticText.Create(DetectPage);
  LinkSampLabel.Parent := DetectPage.Surface;
  LinkSampLabel.Left := 0;
  LinkSampLabel.Caption := CustomMessage('DownloadSampLink');
  ConfigureThemeLinkLabel(LinkSampLabel, '');

  LinkOmpLabel := TNewStaticText.Create(DetectPage);
  LinkOmpLabel.Parent := DetectPage.Surface;
  LinkOmpLabel.Left := 0;
  LinkOmpLabel.Caption := CustomMessage('DownloadOmpLink');
  ConfigureThemeLinkLabel(LinkOmpLabel, '');

  WizardForm.ShowHint := True;

  WelcomeDownloadPreLabel := TNewStaticText.Create(WizardForm.WelcomePage);
  WelcomeDownloadPreLabel.Parent := WizardForm.WelcomePage;
  WelcomeDownloadPreLabel.Caption := CustomMessage('WelcomeDownloadPre');

  WelcomeDownloadLinkLabel := TNewStaticText.Create(WizardForm.WelcomePage);
  WelcomeDownloadLinkLabel.Parent := WizardForm.WelcomePage;
  WelcomeDownloadLinkLabel.Caption := CustomMessage('WelcomeDownloadLink');
  ConfigureThemeLinkLabel(WelcomeDownloadLinkLabel, '{#MyDownloadURL}');
  LayoutPageDownloadLink(WizardForm.WelcomeLabel2, WelcomeDownloadPreLabel, WelcomeDownloadLinkLabel);

  FinishedDownloadPreLabel := TNewStaticText.Create(WizardForm.FinishedPage);
  FinishedDownloadPreLabel.Parent := WizardForm.FinishedPage;
  FinishedDownloadPreLabel.Caption := CustomMessage('FinishedDownloadPre');

  FinishedDownloadLinkLabel := TNewStaticText.Create(WizardForm.FinishedPage);
  FinishedDownloadLinkLabel.Parent := WizardForm.FinishedPage;
  FinishedDownloadLinkLabel.Caption := CustomMessage('FinishedDownloadLink');
  ConfigureThemeLinkLabel(FinishedDownloadLinkLabel, '{#MyDownloadURL}');
  LayoutPageDownloadLink(WizardForm.FinishedLabel, FinishedDownloadPreLabel, FinishedDownloadLinkLabel);
end;

function ValidateSelectedDirectory(const AppDir: string): Boolean;
begin
  Result := False;
  if not FileExists(AppDir + '\gta_sa.exe') then
  begin
    if not WizardSilent then
      MsgBox(CustomMessage('MissingGtaExe'), mbError, MB_OK)
    else
      Log(CustomMessage('MissingGtaExe'));
    Exit;
  end;

  if not FileExists(AppDir + '\samp.dll') or not FileExists(AppDir + '\bass.dll') then
  begin
    if not WizardSilent then
      MsgBox(CustomMessage('MissingSamp'), mbError, MB_OK)
    else
      Log(CustomMessage('MissingSamp'));
    Exit;
  end;

  InspectTargetDirectory(AppDir);

  if (InstalledPlusVersion <> '') and
     (CompareNumericVersions(InstalledPlusVersion, '{#MyAppVersion}') > 0) then
  begin
    if WizardSilent then
    begin
      Log(Format('ERROR: Newer SampVoice Plus (%s) is already installed in %s; silent downgrade to %s aborted.', [InstalledPlusVersion, AppDir, '{#MyAppVersion}']));
      Exit;
    end;
    if (DowngradeConfirmedVersion = '') or
       (CompareNumericVersions(DowngradeConfirmedVersion, InstalledPlusVersion) <> 0) then
    begin
      if MsgBox(FmtMessage(CustomMessage('DowngradePromptFmt'), [InstalledPlusVersion, '{#MyAppVersion}']),
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then
        Exit;
      DowngradeConfirmedVersion := InstalledPlusVersion;
    end;
  end;

  if WizardSilent then
  begin
    if not DetectedSampKnown then
      Log('WARN: ' + FmtMessage(CustomMessage('SampUnknownWarn'), [DetectedSampName]));
    if HasDinputConflict then
    begin
      Log('ERROR: ' + CustomMessage('LoaderConflictText'));
      Exit;
    end;
  end;

  Result := True;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpWelcome then
    LayoutPageDownloadLink(WizardForm.WelcomeLabel2, WelcomeDownloadPreLabel, WelcomeDownloadLinkLabel)
  else if CurPageID = wpFinished then
    LayoutPageDownloadLink(WizardForm.FinishedLabel, FinishedDownloadPreLabel, FinishedDownloadLinkLabel);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrCode: Integer;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    if not ValidateSelectedDirectory(RemoveBackslash(WizardDirValue)) then
    begin
      if not FileExists(RemoveBackslash(WizardDirValue) + '\samp.dll') and not WizardSilent then
      begin
        if MsgBox(CustomMessage('DownloadSampLink') + '?', mbConfirmation, MB_YESNO) = IDYES then
          ShellExec('open', '{#SampDownloadURL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrCode);
      end;
      Result := False;
      Exit;
    end;
    UpdateDetectPageControls;
  end
  else if CurPageID = DetectPage.ID then
  begin
    if HasDinputConflict then
    begin
      MsgBox(CustomMessage('LoaderConflictText'), mbError, MB_OK);
      Result := False;
      Exit;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): string;
var
  AppDir: string;
begin
  Result := '';
  if IsGtaRunning then
  begin
    Log('ERROR: GTA San Andreas is currently running (window class "Grand theft auto San Andreas"); please close it before continuing.');
    Result := CustomMessage('CloseGtaBeforeContinue');
    Exit;
  end;
  if WizardSilent then
  begin
    AppDir := RemoveBackslash(WizardDirValue);
    if not ValidateSelectedDirectory(AppDir) then
    begin
      if not FileExists(AppDir + '\gta_sa.exe') then
        Result := CustomMessage('MissingGtaExe')
      else if (InstalledPlusVersion <> '') and
              (CompareNumericVersions(InstalledPlusVersion, '{#MyAppVersion}') > 0) then
        Result := FmtMessage(CustomMessage('DowngradePromptFmt'), [InstalledPlusVersion, '{#MyAppVersion}'])
      else if HasDinputConflict then
        Result := CustomMessage('LoaderConflictText')
      else
        Result := CustomMessage('MissingSamp');
      Exit;
    end;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: string): string;
var
  S: string;
  Cmp: Integer;
begin
  S := MemoDirInfo + NewLine + NewLine;
  if DetectedSampKnown then
    S := S + FmtMessage(CustomMessage('SampDetectedFmt'), [DetectedSampName]) + NewLine
  else
    S := S + FmtMessage(CustomMessage('SampUnknownWarn'), [DetectedSampName]) + NewLine;

  if ExistingLoaderDesc <> '' then
    S := S + FmtMessage(CustomMessage('LoaderFoundFmt'), [ExistingLoaderDesc]) + NewLine
  else if NeedsInstallUal then
    S := S + CustomMessage('LoaderInstallUal') + NewLine;

  if InstalledPlusVersion <> '' then
  begin
    Cmp := CompareNumericVersions(InstalledPlusVersion, '{#MyAppVersion}');
    if Cmp = 0 then
      S := S + FmtMessage(CustomMessage('SummaryPlusRepairFmt'), [InstalledPlusVersion]) + NewLine
    else if Cmp < 0 then
    begin
      if InstalledPlusVersion = '0.0.0' then
        S := S + FmtMessage(CustomMessage('SummaryPlusUpdateUnknownVerFmt'), ['{#MyAppVersion}']) + NewLine
      else
        S := S + FmtMessage(CustomMessage('SummaryPlusUpdateFmt'), [InstalledPlusVersion, '{#MyAppVersion}']) + NewLine;
    end;
  end;

  if HasLegacyClient then
  begin
    if LegacyRemoveRadio.Checked then
    begin
      S := S + CustomMessage('SummaryLegacyRemove') + NewLine;
      if LegacyConfigCheck.Visible and LegacyConfigCheck.Checked then
        S := S + CustomMessage('SummaryLegacyRemoveCfg') + NewLine;
    end
    else
      S := S + CustomMessage('SummaryLegacyBackup') + NewLine;
  end;

  Result := S;
end;

procedure BackupRelativeTreeCopyOnly(const RootDir, SubRelDir, BackupDir, ManifestPath: string; var RemIdx: Integer);
var
  FindRec: TFindRec;
  SearchPath, SrcFile, RelPath, DstFile: string;
begin
  SearchPath := AddBackslash(RootDir + '\' + SubRelDir) + '*';
  if not FindFirst(SearchPath, FindRec) then
    Exit;
  try
    repeat
      if (FindRec.Name = '.') or (FindRec.Name = '..') then
        Continue;
      if SubRelDir = '' then
        RelPath := FindRec.Name
      else
        RelPath := SubRelDir + '\' + FindRec.Name;

      if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
      begin
        BackupRelativeTreeCopyOnly(RootDir, RelPath, BackupDir, ManifestPath, RemIdx);
      end
      else
      begin
        SrcFile := RootDir + '\' + RelPath;
        DstFile := BackupDir + '\removed\' + RelPath;
        ForceDirectories(ExtractFileDir(DstFile));
        if FileExists(DstFile) or CopyFile(SrcFile, DstFile, False) then
        begin
          if not IsFileListedInManifest(RelPath, 'Removed', ManifestPath) then
          begin
            SetIniString('Removed', Format('File%d', [RemIdx]), RelPath, ManifestPath);
            RemIdx := RemIdx + 1;
          end;
        end;
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

procedure SanitizeManifestDuplicates(const ManifestPath: string; var CreatedIdx, ReplacedIdx, RemovedIdx: Integer);
var
  ReplacedList, CreatedList, RemovedList: TArrayOfString;
  OldReplacedMax, OldCreatedMax, OldRemovedMax: Integer;
  CountHint, I, J, NewCount: Integer;
  Entry: string;
  KeepEntry, Dup: Boolean;
begin
  if not FileExists(ManifestPath) then
    Exit;

  SetArrayLength(ReplacedList, 0);
  CountHint := GetIniInt('Counts', 'Replaced', 0, 0, 100000, ManifestPath);
  I := 0;
  while (I < CountHint) or (GetIniString('Replaced', Format('File%d', [I]), '', ManifestPath) <> '') do
  begin
    Entry := GetIniString('Replaced', Format('File%d', [I]), '', ManifestPath);
    if Entry <> '' then
    begin
      Dup := False;
      for J := 0 to GetArrayLength(ReplacedList) - 1 do
        if CompareText(ReplacedList[J], Entry) = 0 then Dup := True;
      if not Dup then
      begin
        SetArrayLength(ReplacedList, GetArrayLength(ReplacedList) + 1);
        ReplacedList[GetArrayLength(ReplacedList) - 1] := Entry;
      end;
    end;
    I := I + 1;
  end;
  OldReplacedMax := I;

  SetArrayLength(CreatedList, 0);
  CountHint := GetIniInt('Counts', 'Created', 0, 0, 100000, ManifestPath);
  I := 0;
  while (I < CountHint) or (GetIniString('Created', Format('File%d', [I]), '', ManifestPath) <> '') do
  begin
    Entry := GetIniString('Created', Format('File%d', [I]), '', ManifestPath);
    if Entry <> '' then
    begin
      KeepEntry := True;
      for J := 0 to GetArrayLength(ReplacedList) - 1 do
        if CompareText(ReplacedList[J], Entry) = 0 then KeepEntry := False;
      for J := 0 to GetArrayLength(CreatedList) - 1 do
        if CompareText(CreatedList[J], Entry) = 0 then KeepEntry := False;
      if KeepEntry then
      begin
        SetArrayLength(CreatedList, GetArrayLength(CreatedList) + 1);
        CreatedList[GetArrayLength(CreatedList) - 1] := Entry;
      end;
    end;
    I := I + 1;
  end;
  OldCreatedMax := I;

  SetArrayLength(RemovedList, 0);
  CountHint := GetIniInt('Counts', 'Removed', 0, 0, 100000, ManifestPath);
  I := 0;
  while (I < CountHint) or (GetIniString('Removed', Format('File%d', [I]), '', ManifestPath) <> '') do
  begin
    Entry := GetIniString('Removed', Format('File%d', [I]), '', ManifestPath);
    if Entry <> '' then
    begin
      Dup := False;
      for J := 0 to GetArrayLength(RemovedList) - 1 do
        if CompareText(RemovedList[J], Entry) = 0 then Dup := True;
      if not Dup then
      begin
        SetArrayLength(RemovedList, GetArrayLength(RemovedList) + 1);
        RemovedList[GetArrayLength(RemovedList) - 1] := Entry;
      end;
    end;
    I := I + 1;
  end;
  OldRemovedMax := I;

  NewCount := GetArrayLength(ReplacedList);
  for I := 0 to NewCount - 1 do
    SetIniString('Replaced', Format('File%d', [I]), ReplacedList[I], ManifestPath);
  for I := NewCount to OldReplacedMax - 1 do
    DeleteIniEntry('Replaced', Format('File%d', [I]), ManifestPath);
  ReplacedIdx := NewCount;
  SetIniInt('Counts', 'Replaced', ReplacedIdx, ManifestPath);

  NewCount := GetArrayLength(CreatedList);
  for I := 0 to NewCount - 1 do
    SetIniString('Created', Format('File%d', [I]), CreatedList[I], ManifestPath);
  for I := NewCount to OldCreatedMax - 1 do
    DeleteIniEntry('Created', Format('File%d', [I]), ManifestPath);
  CreatedIdx := NewCount;
  SetIniInt('Counts', 'Created', CreatedIdx, ManifestPath);

  NewCount := GetArrayLength(RemovedList);
  for I := 0 to NewCount - 1 do
    SetIniString('Removed', Format('File%d', [I]), RemovedList[I], ManifestPath);
  for I := NewCount to OldRemovedMax - 1 do
    DeleteIniEntry('Removed', Format('File%d', [I]), ManifestPath);
  RemovedIdx := NewCount;
  SetIniInt('Counts', 'Removed', RemovedIdx, ManifestPath);
end;

procedure RecordOrBackupInstalledFile(const AppDir, RelPath, BackupDir, ManifestPath: string;
  var CreatedIdx, ReplacedIdx: Integer);
var
  SrcPath, DstBackup: string;
begin
  if IsFileListedInManifest(RelPath, 'Created', ManifestPath) or
     IsFileListedInManifest(RelPath, 'Replaced', ManifestPath) then
  begin
    // Updating/repairing over an earlier SampVoice Plus installation: preserve original Created/Replaced history
    Exit;
  end;

  SrcPath := AppDir + '\' + RelPath;
  if FileExists(SrcPath) then
  begin
    DstBackup := BackupDir + '\replaced\' + RelPath;
    ForceDirectories(ExtractFileDir(DstBackup));
    if CopyFile(SrcPath, DstBackup, False) then
    begin
      SetIniString('Replaced', Format('File%d', [ReplacedIdx]), RelPath, ManifestPath);
      ReplacedIdx := ReplacedIdx + 1;
      SetIniInt('Counts', 'Replaced', ReplacedIdx, ManifestPath);
    end;
  end
  else
  begin
    SetIniString('Created', Format('File%d', [CreatedIdx]), RelPath, ManifestPath);
    CreatedIdx := CreatedIdx + 1;
    SetIniInt('Counts', 'Created', CreatedIdx, ManifestPath);
  end;
end;

procedure BackupLegacyFileToRemoved(const AppDir, RelPath, BackupDir, ManifestPath: string;
  var RemovedIdx: Integer; const DeleteOriginal: Boolean);
var
  SrcFile, DstFile: string;
begin
  SrcFile := AppDir + '\' + RelPath;
  if not FileExists(SrcFile) then
    Exit;
  DstFile := BackupDir + '\removed\' + RelPath;
  ForceDirectories(ExtractFileDir(DstFile));
  if FileExists(DstFile) or CopyFile(SrcFile, DstFile, False) then
  begin
    if not IsFileListedInManifest(RelPath, 'Removed', ManifestPath) then
    begin
      SetIniString('Removed', Format('File%d', [RemovedIdx]), RelPath, ManifestPath);
      RemovedIdx := RemovedIdx + 1;
      SetIniInt('Counts', 'Removed', RemovedIdx, ManifestPath);
    end;
    if DeleteOriginal then
      DeleteFile(SrcFile);
  end;
end;

procedure BackupScriptsAsiFiltered(const AppDir, BackupDir, ManifestPath: string;
  var RemovedIdx: Integer; const MatchPlus, DeleteOriginal: Boolean);
var
  FindRec: TFindRec;
  RelPath: string;
begin
  if not DirExists(AppDir + '\scripts') then
    Exit;
  if FindFirst(AppDir + '\scripts\sampvoice*.asi', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          RelPath := 'scripts\' + FindRec.Name;
          if IsPlusClientAsi(AppDir + '\' + RelPath) = MatchPlus then
            BackupLegacyFileToRemoved(AppDir, RelPath, BackupDir, ManifestPath, RemovedIdx, DeleteOriginal);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure DeleteLegacyScriptsAsi(const AppDir: string);
var
  FindRec: TFindRec;
  FullPath: string;
begin
  if not DirExists(AppDir + '\scripts') then
    Exit;
  if FindFirst(AppDir + '\scripts\sampvoice*.asi', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          FullPath := AppDir + '\scripts\' + FindRec.Name;
          if not IsPlusClientAsi(FullPath) then
            DeleteFile(FullPath);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CreateBackupManifestBeforeInstall(const AppDir: string);
var
  PlusDir, BackupDir, ManifestPath, LegacyCfgDir, LegacyUninsBase: string;
  CreatedIdx, ReplacedIdx, RemovedIdx, WaitMs, ExecRc: Integer;
  UseRemoveMode, RemoveLegacyCfg, LegacyUninsInAppDir: Boolean;
begin
  PlusDir := AppDir + '\sampvoice-plus';
  BackupDir := PlusDir + '\backup';
  ForceDirectories(PlusDir);
  ManifestPath := PlusDir + '\manifest.ini';

  if FileExists(ManifestPath) then
  begin
    CreatedIdx  := GetIniInt('Counts', 'Created', 0, 0, 100000, ManifestPath);
    ReplacedIdx := GetIniInt('Counts', 'Replaced', 0, 0, 100000, ManifestPath);
    RemovedIdx  := GetIniInt('Counts', 'Removed', 0, 0, 100000, ManifestPath);
    SanitizeManifestDuplicates(ManifestPath, CreatedIdx, ReplacedIdx, RemovedIdx);
  end
  else
  begin
    CreatedIdx  := 0;
    ReplacedIdx := 0;
    RemovedIdx  := 0;
  end;

  if HasLegacyClient then
  begin
    if WizardSilent then
    begin
      UseRemoveMode := CompareText(ExpandConstant('{param:LEGACY|backup}'), 'remove') = 0;
      RemoveLegacyCfg := UseRemoveMode and (CompareText(ExpandConstant('{param:LEGACYCONFIG|keep}'), 'remove') = 0);
    end
    else
    begin
      UseRemoveMode := LegacyRemoveRadio.Checked;
      RemoveLegacyCfg := UseRemoveMode and LegacyConfigCheck.Visible and LegacyConfigCheck.Checked;
    end;

    LegacyUninsBase := '';
    LegacyUninsInAppDir := False;
    if LegacyRegistryFound and (LegacyUninstallerPath <> '') then
    begin
      if CompareText(NormalizeDir(ExtractFileDir(LegacyUninstallerPath)), NormalizeDir(AppDir)) = 0 then
      begin
        LegacyUninsInAppDir := True;
        LegacyUninsBase := ChangeFileExt(ExtractFileName(LegacyUninstallerPath), '');
      end
      else
        Log('WARN: Legacy uninstaller path is outside GTA directory; skipping execution/deletion: ' + LegacyUninstallerPath);
    end;

    // 1. Backup mode only: copy all exclusive legacy files to sampvoice-plus\backup\ before uninstaller/cleanup
    if not UseRemoveMode then
    begin
      if FileExists(AppDir + '\sampvoice.asi') and
         not IsPlusClientAsi(AppDir + '\sampvoice.asi') then
      begin
        RecordOrBackupInstalledFile(AppDir, 'sampvoice.asi', BackupDir, ManifestPath, CreatedIdx, ReplacedIdx);
      end;
      BackupLegacyFileToRemoved(AppDir, 'bass_fx.dll', BackupDir, ManifestPath, RemovedIdx, False);
      BackupScriptsAsiFiltered(AppDir, BackupDir, ManifestPath, RemovedIdx, False, False);
      if DirExists(AppDir + '\sampvoice') then
        BackupRelativeTreeCopyOnly(AppDir, 'sampvoice', BackupDir, ManifestPath, RemovedIdx);
      if LegacyUninsInAppDir and (LegacyUninsBase <> '') then
      begin
        BackupLegacyFileToRemoved(AppDir, LegacyUninsBase + '.exe', BackupDir, ManifestPath, RemovedIdx, False);
        BackupLegacyFileToRemoved(AppDir, LegacyUninsBase + '.dat', BackupDir, ManifestPath, RemovedIdx, False);
      end;
    end;

    // 2. If official legacy setup is registered in AppDir, run it silently and wait up to 60s for _is1 key & uninsXXX.exe removal
    if LegacyUninsInAppDir and (LegacyUninsBase <> '') and FileExists(LegacyUninstallerPath) then
    begin
      Log('SV+ Running legacy uninstaller: ' + LegacyUninstallerPath);
      Exec(LegacyUninstallerPath, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ExecRc);
      WaitMs := 0;
      while (WaitMs < 60000) and (IsLegacyRegistryKeyPresent(AppDir) or FileExists(AppDir + '\' + LegacyUninsBase + '.exe')) do
      begin
        Sleep(250);
        WaitMs := WaitMs + 250;
      end;
      if IsLegacyRegistryKeyPresent(AppDir) or FileExists(AppDir + '\' + LegacyUninsBase + '.exe') then
        Log('WARN: Timed out waiting 60s for legacy uninstaller cleanup; continuing to fallback cleanup')
      else
        Log(Format('SV+ Legacy uninstaller completed and registry cleared in %d ms', [WaitMs]));
    end;

    // 3. Remove any leftover legacy exclusive files (never touch shared bass.dll, vorbisFile.dll, vorbisHooked.dll)
    if FileExists(AppDir + '\sampvoice.asi') and
       not IsPlusClientAsi(AppDir + '\sampvoice.asi') then
      DeleteFile(AppDir + '\sampvoice.asi');
    if FileExists(AppDir + '\bass_fx.dll') then
      DeleteFile(AppDir + '\bass_fx.dll');
    DeleteLegacyScriptsAsi(AppDir);
    if DirExists(AppDir + '\sampvoice') then
      DelTree(AppDir + '\sampvoice', True, True, True);
    if LegacyUninsInAppDir and (LegacyUninsBase <> '') then
    begin
      if FileExists(AppDir + '\' + LegacyUninsBase + '.exe') then
        DeleteFile(AppDir + '\' + LegacyUninsBase + '.exe');
      if FileExists(AppDir + '\' + LegacyUninsBase + '.dat') then
        DeleteFile(AppDir + '\' + LegacyUninsBase + '.dat');
    end;

    // 4. Uninstall mode + checkbox: delete legacy Documents configuration folder
    if UseRemoveMode and RemoveLegacyCfg then
    begin
      LegacyCfgDir := GetLegacyConfigDir;
      if DirExists(LegacyCfgDir) then
      begin
        if DelTree(LegacyCfgDir, True, True, True) then
          Log('SV+ Removed legacy Documents config folder: ' + LegacyCfgDir)
        else
          Log('WARN: Failed to fully remove legacy Documents config folder: ' + LegacyCfgDir);
      end
      else
        Log('SV+ Legacy Documents config folder not found (nothing to delete): ' + LegacyCfgDir);
    end;

    // 5. Record [Legacy] decision in manifest for logging and uninstall restoration
    if UseRemoveMode then
    begin
      SetIniString('Legacy', 'Mode', 'removed', ManifestPath);
      SetIniBool('Legacy', 'ConfigRemoved', RemoveLegacyCfg, ManifestPath);
      Log('SV+ Legacy handling mode recorded: removed');
    end
    else
    begin
      SetIniString('Legacy', 'Mode', 'backup', ManifestPath);
      Log('SV+ Legacy handling mode recorded: backup');
    end;
  end;

  // Migrate any manual SampVoice Plus ASI in scripts\ so only the root sampvoice.asi remains active,
  // while preserving a restorable backup under [Removed] for clean uninstall rollback.
  BackupScriptsAsiFiltered(AppDir, BackupDir, ManifestPath, RemovedIdx, True, True);

  RecordOrBackupInstalledFile(AppDir, 'sampvoice.asi', BackupDir, ManifestPath, CreatedIdx, ReplacedIdx);
  RecordOrBackupInstalledFile(AppDir, 'sampvoice-plus\THIRD-PARTY-NOTICES.txt', BackupDir, ManifestPath, CreatedIdx, ReplacedIdx);

  if ShouldInstallUal then
    RecordOrBackupInstalledFile(AppDir, 'dinput8.dll', BackupDir, ManifestPath, CreatedIdx, ReplacedIdx);

  SetIniInt('Counts', 'Created', CreatedIdx, ManifestPath);
  SetIniInt('Counts', 'Replaced', ReplacedIdx, ManifestPath);
  SetIniInt('Counts', 'Removed', RemovedIdx, ManifestPath);
end;

procedure RecordInstalledHashesAndIni(const AppDir: string);
var
  ManifestPath, IniDir, IniPath: string;
  Content: AnsiString;
begin
  ManifestPath := AppDir + '\sampvoice-plus\manifest.ini';
  if FileExists(AppDir + '\sampvoice.asi') then
    SetIniString('Hashes', 'sampvoice.asi', GetSHA256OfFile(AppDir + '\sampvoice.asi'), ManifestPath);
  if FileExists(AppDir + '\sampvoice-plus\THIRD-PARTY-NOTICES.txt') then
    SetIniString('Hashes', 'sampvoice-plus\THIRD-PARTY-NOTICES.txt', GetSHA256OfFile(AppDir + '\sampvoice-plus\THIRD-PARTY-NOTICES.txt'), ManifestPath);
  if (ShouldInstallUal or PrevInstalledUal) and FileExists(AppDir + '\dinput8.dll') then
    SetIniString('Hashes', 'dinput8.dll', GetSHA256OfFile(AppDir + '\dinput8.dll'), ManifestPath);

  IniDir := ExpandConstant('{userdocs}\GTA San Andreas User Files\SAMP\sampvoice');
  IniPath := IniDir + '\sampvoice.ini';
  if not FileExists(IniPath) then
  begin
    ForceDirectories(IniDir);
    Content := AnsiString('; SampVoice Plus Configuration' + #13#10 +
                          '[general]' + #13#10 +
                          'language=' + ActiveLanguage + #13#10);
    if SaveStringToFile(IniPath, Content, False) then
    begin
      SetIniBool('Ini', 'Created', True, ManifestPath);
      SetIniString('Ini', 'Path', IniPath, ManifestPath);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  AppDir: string;
begin
  AppDir := RemoveBackslash(ExpandConstant('{app}'));
  if CurStep = ssInstall then
    CreateBackupManifestBeforeInstall(AppDir)
  else if CurStep = ssPostInstall then
    RecordInstalledHashesAndIni(AppDir);
end;

function MatchesExpectedHash(const FilePath, RelPath, ManifestPath: string): Boolean;
var
  Expected, Actual: string;
begin
  Expected := GetIniString('Hashes', RelPath, '', ManifestPath);
  if Expected = '' then
  begin
    Result := True;
    Exit;
  end;
  Actual := GetSHA256OfFile(FilePath);
  Result := CompareText(Actual, Expected) = 0;
end;

procedure RestoreManifestAndClean(const AppDir: string);
var
  PlusDir, BackupDir, ManifestPath, RelPath, TargetPath, SrcPath, IniPath, LegacyMode: string;
  Count, I: Integer;
  AskRemoveIni, RestoreFailed: Boolean;
begin
  PlusDir := AppDir + '\sampvoice-plus';
  BackupDir := PlusDir + '\backup';
  ManifestPath := PlusDir + '\manifest.ini';
  if not FileExists(ManifestPath) then
    Exit;

  RestoreFailed := False;
  LegacyMode := GetIniString('Legacy', 'Mode', 'backup', ManifestPath);

  // 1. Delete files newly created by installer if unmodified
  Count := GetIniInt('Counts', 'Created', 0, 0, 100000, ManifestPath);
  for I := 0 to Count - 1 do
  begin
    RelPath := GetIniString('Created', Format('File%d', [I]), '', ManifestPath);
    if RelPath = '' then Continue;
    TargetPath := AppDir + '\' + RelPath;
    if FileExists(TargetPath) then
    begin
      if MatchesExpectedHash(TargetPath, RelPath, ManifestPath) then
      begin
        if not DeleteFile(TargetPath) then
        begin
          Log('ERROR: Failed to delete created file during uninstall: ' + TargetPath);
          RestoreFailed := True;
        end;
      end
      else if not UninstallSilent then
        MsgBox(FmtMessage(CustomMessage('UninstallKeepChangedWarn'), [RelPath]), mbInformation, MB_OK);
    end;
  end;

  // 2. Restore files replaced by installer if current file matches what we installed
  if CompareText(LegacyMode, 'removed') <> 0 then
  begin
    Count := GetIniInt('Counts', 'Replaced', 0, 0, 100000, ManifestPath);
    for I := 0 to Count - 1 do
    begin
      RelPath := GetIniString('Replaced', Format('File%d', [I]), '', ManifestPath);
      if RelPath = '' then Continue;
      TargetPath := AppDir + '\' + RelPath;
      SrcPath := BackupDir + '\replaced\' + RelPath;
      if FileExists(SrcPath) then
      begin
        if (not FileExists(TargetPath)) or MatchesExpectedHash(TargetPath, RelPath, ManifestPath) then
        begin
          ForceDirectories(ExtractFileDir(TargetPath));
          if not CopyFile(SrcPath, TargetPath, False) then
          begin
            Log(Format('ERROR: Failed to restore replaced file %s -> %s', [SrcPath, TargetPath]));
            RestoreFailed := True;
          end;
        end
        else if not UninstallSilent then
          MsgBox(FmtMessage(CustomMessage('UninstallKeepChangedWarn'), [RelPath]), mbInformation, MB_OK);
      end;
    end;

    // 3. Restore legacy files backed up during migration
    Count := GetIniInt('Counts', 'Removed', 0, 0, 100000, ManifestPath);
    for I := 0 to Count - 1 do
    begin
      RelPath := GetIniString('Removed', Format('File%d', [I]), '', ManifestPath);
      if RelPath = '' then Continue;
      TargetPath := AppDir + '\' + RelPath;
      SrcPath := BackupDir + '\removed\' + RelPath;
      if FileExists(SrcPath) then
      begin
        ForceDirectories(ExtractFileDir(TargetPath));
        if not CopyFile(SrcPath, TargetPath, False) then
        begin
          Log(Format('ERROR: Failed to restore backed-up file %s -> %s', [SrcPath, TargetPath]));
          RestoreFailed := True;
        end;
      end;
    end;
  end;

  // 4. Optional Documents settings cleanup
  IniPath := ExpandConstant('{userdocs}\GTA San Andreas User Files\SAMP\sampvoice');
  if DirExists(IniPath) and not UninstallSilent then
  begin
    AskRemoveIni := MsgBox(CustomMessage('UninstallRemoveIni'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    if AskRemoveIni then
      DelTree(IniPath, True, True, True);
  end;

  // 5. Remove backup subtree and manifest ONLY if every deletion/restoration succeeded
  if RestoreFailed then
  begin
    Log('ERROR: One or more uninstall file operations failed; preserving backup folder and manifest at ' + PlusDir);
    if not UninstallSilent then
      MsgBox(FmtMessage(CustomMessage('UninstallRestoreFailedWarn'), [BackupDir]), mbError, MB_OK);
  end
  else
  begin
    if DirExists(BackupDir) then
      DelTree(BackupDir, True, True, True);
    DeleteFile(ManifestPath);
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  while IsGtaRunning do
  begin
    if UninstallSilent then
    begin
      Log('ERROR: GTA San Andreas is currently running (window class "Grand theft auto San Andreas"); silent uninstall aborted.');
      Result := False;
      Exit;
    end;
    if MsgBox(CustomMessage('CloseGtaBeforeContinue'), mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Log('SV+ Uninstall cancelled by user while GTA San Andreas was running.');
      Result := False;
      Exit;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDir: string;
begin
  AppDir := RemoveBackslash(ExpandConstant('{app}'));
  if CurUninstallStep = usUninstall then
    RestoreManifestAndClean(AppDir)
  else if CurUninstallStep = usPostUninstall then
    RemoveDir(AppDir + '\sampvoice-plus');
end;
