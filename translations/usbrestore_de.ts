<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de" sourcelanguage="en">
<context>
    <name>usbrestore::Backend</name>
    <message>
        <source>The restore failed.</source>
        <translation>Die Wiederherstellung ist fehlgeschlagen.</translation>
    </message>
    <message>
        <source>the loop driver is busy with another mapping</source>
        <translation>der Loop-Treiber wird von einer anderen Zuordnung verwendet</translation>
    </message>
    <message>
        <source>the system refused access to the loop driver</source>
        <translation>das System hat den Zugriff auf den Loop-Treiber verweigert</translation>
    </message>
    <message>
        <source>the loop driver is not available</source>
        <translation>der Loop-Treiber ist nicht verfügbar</translation>
    </message>
    <message>
        <source>the loop device went away</source>
        <translation>das Loop-Gerät ist nicht mehr verfügbar</translation>
    </message>
    <message>
        <source>the loop mapping was rejected</source>
        <translation>die Loop-Zuordnung wurde abgelehnt</translation>
    </message>
    <message>
        <source>the drive failed while writing</source>
        <translation>beim Schreiben auf das Laufwerk ist ein Fehler aufgetreten</translation>
    </message>
    <message>
        <source>the loop mapping ran out of room</source>
        <translation>in der Loop-Zuordnung ist kein Speicherplatz mehr verfügbar</translation>
    </message>
    <message>
        <source>%1: %2 (errno %3). %4.</source>
        <translation>%1: %2 (errno %3). %4.</translation>
    </message>
    <message>
        <source>A formatting area is already set up for this drive. Unplug the drive, plug it back in and try again.</source>
        <translation>Für dieses Laufwerk ist bereits ein Formatierungsbereich eingerichtet. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>Could not read the drive through the formatting area</source>
        <translation>Das Laufwerk konnte über den Formatierungsbereich nicht gelesen werden</translation>
    </message>
    <message>
        <source>Try the restore again</source>
        <translation>Starten Sie die Wiederherstellung erneut</translation>
    </message>
    <message>
        <source>The drive could not be read for formatting. Unplug it, plug it back in and try again.</source>
        <translation>Das Laufwerk konnte zum Formatieren nicht gelesen werden. Ziehen Sie es ab, stecken Sie es wieder ein und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>The space set aside for the new volume is outside the drive or is not aligned to its sectors. Refresh the list and select the drive again.</source>
        <translation>Der für das neue Volume vorgesehene Bereich liegt außerhalb des Laufwerks oder ist nicht an dessen Sektoren ausgerichtet. Aktualisieren Sie die Liste und wählen Sie das Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>Could not reach the Linux loop driver at /dev/loop-control, which formatting needs</source>
        <translation>Der zum Formatieren benötigte Linux-Loop-Treiber unter /dev/loop-control ist nicht erreichbar</translation>
    </message>
    <message>
        <source>Make sure the loop module is loaded (modprobe loop) and try again</source>
        <translation>Stellen Sie sicher, dass das Loop-Modul geladen ist (modprobe loop), und versuchen Sie es erneut</translation>
    </message>
    <message>
        <source>Could not set up the temporary area used to format the drive</source>
        <translation>Der temporäre Bereich zum Formatieren des Laufwerks konnte nicht eingerichtet werden</translation>
    </message>
    <message>
        <source>Close other disk tools that may be holding loop devices, such as GNOME Disks, and try again</source>
        <translation>Schließen Sie andere Datenträgerprogramme, die Loop-Geräte verwenden könnten, etwa GNOME Laufwerke, und versuchen Sie es erneut</translation>
    </message>
    <message>
        <source>Could not make the new partition available to the formatter</source>
        <translation>Die neue Partition konnte dem Formatierungsprogramm nicht bereitgestellt werden</translation>
    </message>
    <message>
        <source>Unplug the drive, plug it back in and try again</source>
        <translation>Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und versuchen Sie es erneut</translation>
    </message>
    <message>
        <source>Could not finish writing the new volume to the drive</source>
        <translation>Das Schreiben des neuen Volumes auf das Laufwerk konnte nicht abgeschlossen werden</translation>
    </message>
    <message>
        <source>Unplug the drive, plug it back in and check whether the volume appears; if it does not, run the restore again</source>
        <translation>Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und prüfen Sie, ob das Volume angezeigt wird; falls nicht, starten Sie die Wiederherstellung erneut</translation>
    </message>
    <message>
        <source>The new volume was written, but its temporary formatting area could not be closed</source>
        <translation>Das neue Volume wurde geschrieben, aber sein temporärer Formatierungsbereich konnte nicht geschlossen werden</translation>
    </message>
    <message>
        <source>Unplug the drive and plug it back in; this does not damage the drive</source>
        <translation>Ziehen Sie das Laufwerk ab und stecken Sie es wieder ein; dadurch wird es nicht beschädigt</translation>
    </message>
    <message>
        <source>another program still has it open</source>
        <translation>ein anderes Programm hält das Laufwerk noch geöffnet</translation>
    </message>
    <message>
        <source>Close that program, unmount every partition on the drive and try again</source>
        <translation>Schließen Sie dieses Programm, hängen Sie alle Partitionen des Laufwerks aus und versuchen Sie es erneut</translation>
    </message>
    <message>
        <source>the drive would not accept writes</source>
        <translation>das Laufwerk hat Schreibzugriffe abgelehnt</translation>
    </message>
    <message>
        <source>If it has a write-protect switch, turn it off and try again. If it has none, the drive may have made itself read-only, which worn drives do, and needs replacing</source>
        <translation>Falls das Laufwerk einen Schreibschutzschalter hat, deaktivieren Sie ihn und versuchen Sie es erneut. Andernfalls hat es sich möglicherweise wegen Verschleiß selbst schreibgeschützt und muss ersetzt werden</translation>
    </message>
    <message>
        <source>the drive stopped answering</source>
        <translation>das Laufwerk antwortet nicht mehr</translation>
    </message>
    <message>
        <source>Unplug it, plug it back in and try again; if it keeps happening the drive is damaged</source>
        <translation>Ziehen Sie es ab, stecken Sie es wieder ein und versuchen Sie es erneut; tritt der Fehler weiterhin auf, ist das Laufwerk beschädigt</translation>
    </message>
    <message>
        <source>the drive is no longer there</source>
        <translation>das Laufwerk ist nicht mehr verfügbar</translation>
    </message>
    <message>
        <source>Plug it back in, refresh the list and select it again</source>
        <translation>Stecken Sie es wieder ein, aktualisieren Sie die Liste und wählen Sie es erneut aus</translation>
    </message>
    <message>
        <source>the drive ran out of room before its reported size</source>
        <translation>auf dem Laufwerk ist schon vor Erreichen der gemeldeten Größe kein Speicherplatz mehr verfügbar</translation>
    </message>
    <message>
        <source>Unplug it, plug it back in and try again; if it happens again the drive is smaller than it claims</source>
        <translation>Ziehen Sie es ab, stecken Sie es wieder ein und versuchen Sie es erneut; tritt der Fehler erneut auf, ist das Laufwerk kleiner als angegeben</translation>
    </message>
    <message>
        <source>%1 is still in use. Close anything reading from the drive, make sure every partition is unmounted, and try again.</source>
        <translation>%1 wird noch verwendet. Schließen Sie alle Programme, die auf das Laufwerk zugreifen, hängen Sie alle Partitionen aus und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>Could not open %1</source>
        <translation>%1 konnte nicht geöffnet werden</translation>
    </message>
    <message>
        <source>%1 is not open, so nothing could be checked on it.</source>
        <translation>%1 ist nicht geöffnet und konnte daher nicht überprüft werden.</translation>
    </message>
    <message>
        <source>Could not read the device information of %1</source>
        <translation>Die Geräteinformationen von %1 konnten nicht gelesen werden</translation>
    </message>
    <message>
        <source>%1 is not a disk device.</source>
        <translation>%1 ist kein Datenträgergerät.</translation>
    </message>
    <message>
        <source>%1 does not report which instance of the drive it is, so the tool cannot be sure this is the disk you selected. Linux restores need kernel 5.15 or newer (check with `uname -r`).</source>
        <translation>%1 meldet keine Laufwerksinstanz. Daher kann nicht sichergestellt werden, dass dies der ausgewählte Datenträger ist. Unter Linux wird Kernel 5.15 oder neuer benötigt (prüfen mit `uname -r`).</translation>
    </message>
    <message>
        <source>%1 is not listed under /sys/block as a whole disk. Select the drive itself, such as /dev/sdb rather than /dev/sdb1.</source>
        <translation>%1 ist unter /sys/block nicht als vollständiger Datenträger aufgeführt. Wählen Sie das Laufwerk selbst aus, etwa /dev/sdb statt /dev/sdb1.</translation>
    </message>
    <message>
        <source>%1 no longer points at the disk that was selected. Refresh and select the USB again.</source>
        <translation>%1 verweist nicht mehr auf den ausgewählten Datenträger. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>%1 reports no usable media. The drive may be empty, damaged, or not fully plugged in.</source>
        <translation>%1 meldet kein nutzbares Medium. Das Laufwerk ist möglicherweise leer, beschädigt oder nicht vollständig eingesteckt.</translation>
    </message>
    <message>
        <source>%1 now holds a %2 drive, not the %3 that was selected. Refresh and select the USB again.</source>
        <translation>%1 enthält jetzt ein Laufwerk mit %2 statt der ausgewählten %3. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>%1 uses %2-byte sectors, not the %3-byte sectors the layout was built for. Refresh and select the USB again.</source>
        <translation>%1 verwendet Sektoren mit %2 Byte statt der für das Partitionsschema vorgesehenen %3 Byte. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>%1 is write-protected and cannot be formatted. Check for a write-protect switch on the drive.</source>
        <translation>%1 ist schreibgeschützt und kann nicht formatiert werden. Prüfen Sie, ob das Laufwerk einen Schreibschutzschalter hat.</translation>
    </message>
    <message>
        <source>%1 is not on the USB bus.</source>
        <translation>%1 ist nicht am USB-Bus angeschlossen.</translation>
    </message>
    <message>
        <source>%1 is not open, so the new partition could not be checked.</source>
        <translation>%1 ist nicht geöffnet. Die neue Partition konnte daher nicht überprüft werden.</translation>
    </message>
    <message>
        <source>%1 did not appear as a partition of /dev/%2. Unplug the drive, plug it back in and try again.</source>
        <translation>%1 wurde nicht als Partition von /dev/%2 angezeigt. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>%1 is not the drive the partition table was written to: a different disk took its place during the restore.</source>
        <translation>%1 ist nicht das Laufwerk, auf das die Partitionstabelle geschrieben wurde: Ein anderer Datenträger hat während der Wiederherstellung seinen Platz eingenommen.</translation>
    </message>
    <message>
        <source>%1 is not open, so nothing could be written to it.</source>
        <translation>%1 ist nicht geöffnet und konnte daher nicht beschrieben werden.</translation>
    </message>
    <message>
        <source>Could not write to %1</source>
        <translation>Auf %1 konnte nicht geschrieben werden</translation>
    </message>
    <message>
        <source>%1 stopped accepting data part way through the write. Unplug the drive, plug it back in and try again; if it keeps happening the drive is damaged.</source>
        <translation>%1 nimmt während des Schreibens keine Daten mehr an. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und versuchen Sie es erneut; tritt der Fehler weiterhin auf, ist das Laufwerk beschädigt.</translation>
    </message>
    <message>
        <source>Could not save the changes to %1</source>
        <translation>Die Änderungen auf %1 konnten nicht gespeichert werden</translation>
    </message>
    <message>
        <source>%1 is not open, so its partition table could not be re-read.</source>
        <translation>%1 ist nicht geöffnet. Die Partitionstabelle konnte daher nicht neu eingelesen werden.</translation>
    </message>
    <message>
        <source>The new partition table was written to %1, but the system would not re-scan it</source>
        <translation>Die neue Partitionstabelle wurde auf %1 geschrieben, aber das System konnte sie nicht neu einlesen</translation>
    </message>
    <message>
        <source>/sys/block is not available, so block devices cannot be listed.</source>
        <translation>/sys/block ist nicht verfügbar. Blockgeräte können daher nicht aufgelistet werden.</translation>
    </message>
    <message>
        <source>%1 is no longer present. Refresh and select the USB again.</source>
        <translation>%1 ist nicht mehr vorhanden. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>%1 is no longer reported on the USB bus. Refresh and select the USB again.</source>
        <translation>%1 wird nicht mehr am USB-Bus gemeldet. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>%1 is mounted at %2, which the running system needs. Pick a different drive.</source>
        <translation>%1 ist unter %2 eingehängt und wird vom laufenden System benötigt. Wählen Sie ein anderes Laufwerk.</translation>
    </message>
    <message>
        <source>Could not unmount %1</source>
        <translation>%1 konnte nicht ausgehängt werden</translation>
    </message>
    <message>
        <source>Could not run %1, the tool that formats the drive. Check that it is still installed and that the system allows it to run, then try again.</source>
        <translation>%1, das Programm zum Formatieren des Laufwerks, konnte nicht gestartet werden. Prüfen Sie, ob es noch installiert ist und vom System ausgeführt werden darf, und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>%1 did not finish formatting after 15 minutes and was stopped. The drive may be partly formatted: unplug it, plug it back in and run the restore again.</source>
        <translation>%1 hat die Formatierung nach 15 Minuten nicht abgeschlossen und wurde beendet. Das Laufwerk ist möglicherweise nur teilweise formatiert: Ziehen Sie es ab, stecken Sie es wieder ein und starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>%1 failed to format the new volume on %2. Unplug the drive, plug it back in and run the restore again.</source>
        <translation>%1 konnte das neue Volume auf %2 nicht formatieren. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>%1 failed to format the new volume on %2: %3

Unplug the drive, plug it back in and run the restore again.</source>
        <translation>%1 konnte das neue Volume auf %2 nicht formatieren: %3

Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>mkfs.exfat was not found. Install exfatprogs (Debian/Ubuntu and Fedora: &quot;exfatprogs&quot;, Arch: &quot;exfatprogs&quot;) and try again.</source>
        <translation>mkfs.exfat wurde nicht gefunden. Installieren Sie exfatprogs (Debian/Ubuntu und Fedora: &quot;exfatprogs&quot;, Arch: &quot;exfatprogs&quot;) und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>mkfs.vfat was not found. Install dosfstools and try again.</source>
        <translation>mkfs.vfat wurde nicht gefunden. Installieren Sie dosfstools und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>mkfs.ntfs was not found. Install ntfs-3g (Debian/Ubuntu: &quot;ntfs-3g&quot;, Fedora: &quot;ntfsprogs&quot;, Arch: &quot;ntfs-3g&quot;) and try again.</source>
        <translation>mkfs.ntfs wurde nicht gefunden. Installieren Sie ntfs-3g (Debian/Ubuntu: &quot;ntfs-3g&quot;, Fedora: &quot;ntfsprogs&quot;, Arch: &quot;ntfs-3g&quot;) und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>mkfs.ext4 was not found. Install e2fsprogs and try again.</source>
        <translation>mkfs.ext4 wurde nicht gefunden. Installieren Sie e2fsprogs und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>That filesystem is not supported.</source>
        <translation>Dieses Dateisystem wird nicht unterstützt.</translation>
    </message>
    <message>
        <source>A disk with %1-byte sectors cannot be formatted as FAT32.</source>
        <translation>Ein Datenträger mit Sektoren von %1 Byte kann nicht als FAT32 formatiert werden.</translation>
    </message>
    <message>
        <source>FAT32 cannot address a volume larger than %1 with %2-byte sectors, and the rest of the drive would be left unused. Choose exFAT or NTFS.</source>
        <translation>FAT32 kann mit Sektoren von %2 Byte kein Volume über %1 adressieren. Der übrige Speicherplatz bliebe ungenutzt. Wählen Sie exFAT oder NTFS.</translation>
    </message>
    <message>
        <source>This drive is too small for a valid FAT32 filesystem; it needs at least %1. Choose exFAT.</source>
        <translation>Dieses Laufwerk ist zu klein für ein gültiges FAT32-Dateisystem; benötigt werden mindestens %1. Wählen Sie exFAT.</translation>
    </message>
    <message>
        <source>Formatting as %1</source>
        <translation>Als %1 formatieren</translation>
    </message>
    <message>
        <source>Re-reading the partition table</source>
        <translation>Partitionstabelle neu einlesen</translation>
    </message>
    <message>
        <source>The drive %1 was formatted, but it could not be re-identified afterwards. Unplug it, plug it back in and check that it shows one new partition.</source>
        <translation>Das Laufwerk %1 wurde formatiert, konnte danach aber nicht erneut identifiziert werden. Ziehen Sie es ab, stecken Sie es wieder ein und prüfen Sie, ob eine neue Partition angezeigt wird.</translation>
    </message>
    <message>
        <source>Verifying the selected USB disk</source>
        <translation>Ausgewählten USB-Datenträger überprüfen</translation>
    </message>
    <message>
        <source>Identifying the block device</source>
        <translation>Blockgerät identifizieren</translation>
    </message>
    <message>
        <source>Unmounting existing filesystems</source>
        <translation>Vorhandene Dateisysteme aushängen</translation>
    </message>
    <message>
        <source>Opening the device exclusively</source>
        <translation>Gerät exklusiv öffnen</translation>
    </message>
    <message>
        <source>Clearing old MBR, GPT and ISO signatures</source>
        <translation>Alte MBR-, GPT- und ISO-Signaturen löschen</translation>
    </message>
    <message>
        <source>%1 reports too little space to clear its old partition signatures. Unplug the drive, plug it back in and select it again; if it still reports a tiny size, the drive is damaged.</source>
        <translation>%1 meldet zu wenig Speicherplatz zum Löschen der alten Partitionssignaturen. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und wählen Sie es erneut aus; meldet es weiterhin eine winzige Größe, ist es beschädigt.</translation>
    </message>
    <message>
        <source>Writing the %1 partition table</source>
        <translation>%1-Partitionstabelle schreiben</translation>
    </message>
    <message>
        <source>Waiting for the new partition to appear</source>
        <translation>Auf die neue Partition warten</translation>
    </message>
    <message>
        <source>The new partition did not appear on %1 within %2 seconds. Unplug the drive, plug it back in and run the restore again.</source>
        <translation>Die neue Partition wurde auf %1 nicht innerhalb von %2 Sekunden angezeigt. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>Restore complete</source>
        <translation>Wiederherstellung abgeschlossen</translation>
    </message>
    <message>
        <source>The restore helper at %1 could not be started.</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung unter %1 konnte nicht gestartet werden.</translation>
    </message>
    <message>
        <source>The restore helper was not found at %1. USB Restoration Tool has to be installed to restore a drive without running the whole application as root.</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung wurde unter %1 nicht gefunden. USB Restoration Tool muss installiert sein, um ein Laufwerk wiederherzustellen, ohne die gesamte Anwendung als root auszuführen.</translation>
    </message>
    <message>
        <source>pkexec was not found, so permission to restore the drive cannot be requested. Install polkit, or run USB Restoration Tool with sudo.</source>
        <translation>pkexec wurde nicht gefunden. Die Berechtigung zur Wiederherstellung kann daher nicht angefordert werden. Installieren Sie polkit oder starten Sie USB Restoration Tool mit sudo.</translation>
    </message>
    <message>
        <source>pkexec could not be started, so permission to restore the drive could not be requested.</source>
        <translation>pkexec konnte nicht gestartet werden. Die Berechtigung zur Wiederherstellung konnte daher nicht angefordert werden.</translation>
    </message>
    <message>
        <source>Disk %1 is no longer reported as a USB disk. Refresh and select the USB again.</source>
        <translation>Datenträger %1 wird nicht mehr als USB-Datenträger gemeldet. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>open the drive</source>
        <translation>das Laufwerk öffnen</translation>
    </message>
    <message>
        <source>Could not verify disk %1 because the drive is not open. Run the restore again.</source>
        <translation>Datenträger %1 konnte nicht überprüft werden, weil das Laufwerk nicht geöffnet ist. Starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>read the device number</source>
        <translation>die Gerätenummer lesen</translation>
    </message>
    <message>
        <source>Could not verify disk %1 because the opened device is not a disk. Refresh the list and select the USB drive again.</source>
        <translation>Datenträger %1 konnte nicht überprüft werden, weil das geöffnete Gerät kein Datenträger ist. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>The opened handle points at disk %1, not the whole of disk %2. Refresh the list and select the USB drive again.</source>
        <translation>Das geöffnete Handle verweist auf Datenträger %1 statt auf den gesamten Datenträger %2. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>read the storage properties</source>
        <translation>die Speichereigenschaften lesen</translation>
    </message>
    <message>
        <source>Disk %1 is not on the USB bus. Refresh the list and select a USB drive.</source>
        <translation>Datenträger %1 ist nicht am USB-Bus angeschlossen. Aktualisieren Sie die Liste und wählen Sie ein USB-Laufwerk.</translation>
    </message>
    <message>
        <source>Disk %1 reports a different serial number than the disk that was selected. Refresh the list and select the USB drive again.</source>
        <translation>Datenträger %1 meldet eine andere Seriennummer als der ausgewählte Datenträger. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>read the drive size</source>
        <translation>die Laufwerksgröße lesen</translation>
    </message>
    <message>
        <source>Disk %1 now reports %2 instead of the %3 that was selected. Refresh the list and select the USB drive again.</source>
        <translation>Datenträger %1 meldet jetzt %2 statt der ausgewählten %3. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>Disk %1 reports %2-byte sectors instead of the %3-byte sectors that were selected. Refresh the list and select the USB drive again.</source>
        <translation>Datenträger %1 meldet Sektoren mit %2 Byte statt der ausgewählten %3 Byte. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>check write access</source>
        <translation>den Schreibzugriff prüfen</translation>
    </message>
    <message>
        <source>Could not lock the drive on disk %1</source>
        <translation>Das Laufwerk auf Datenträger %1 konnte nicht gesperrt werden</translation>
    </message>
    <message>
        <source>enable extended disk I/O</source>
        <translation>erweiterte Datenträger-Ein-/Ausgabe aktivieren</translation>
    </message>
    <message>
        <source>refresh the disk properties</source>
        <translation>die Datenträgereigenschaften aktualisieren</translation>
    </message>
    <message>
        <source>Disk %1 is too small to clear safely. Select a larger USB drive.</source>
        <translation>Datenträger %1 ist zum sicheren Löschen zu klein. Wählen Sie ein größeres USB-Laufwerk.</translation>
    </message>
    <message>
        <source>reset the partition table</source>
        <translation>die Partitionstabelle zurücksetzen</translation>
    </message>
    <message>
        <source>Windows still lists a partition on disk %1 after an empty partition table was written. Unplug the USB disk, plug it back in and run the restore again.</source>
        <translation>Windows führt auf Datenträger %1 nach dem Schreiben einer leeren Partitionstabelle weiterhin eine Partition auf. Ziehen Sie den USB-Datenträger ab, stecken Sie ihn wieder ein und starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>Could not generate an identifier for a partition table on disk %1. Run the restore again.</source>
        <translation>Für eine Partitionstabelle auf Datenträger %1 konnte keine Kennung erzeugt werden. Starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>write the GPT partition table</source>
        <translation>die GPT-Partitionstabelle schreiben</translation>
    </message>
    <message>
        <source>read the partition table</source>
        <translation>die Partitionstabelle lesen</translation>
    </message>
    <message>
        <source>Windows returned an incomplete partition table for disk %1. Unplug the drive, plug it back in and try again.</source>
        <translation>Windows hat für Datenträger %1 eine unvollständige Partitionstabelle zurückgegeben. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>Could not create a partition on disk %1: %2 Select a supported partition layout or a different USB drive.</source>
        <translation>Auf Datenträger %1 konnte keine Partition erstellt werden: %2 Wählen Sie ein unterstütztes Partitionsschema oder ein anderes USB-Laufwerk.</translation>
    </message>
    <message>
        <source>Could not generate a partition identifier for disk %1. Run the restore again.</source>
        <translation>Für Datenträger %1 konnte keine Partitionskennung erzeugt werden. Starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>create the GPT partition</source>
        <translation>die GPT-Partition erstellen</translation>
    </message>
    <message>
        <source>Could not generate an MBR signature for disk %1. Run the restore again.</source>
        <translation>Für Datenträger %1 konnte keine MBR-Signatur erzeugt werden. Starten Sie die Wiederherstellung erneut.</translation>
    </message>
    <message>
        <source>write the MBR partition table</source>
        <translation>die MBR-Partitionstabelle schreiben</translation>
    </message>
    <message>
        <source>create the MBR partition</source>
        <translation>die MBR-Partition erstellen</translation>
    </message>
    <message>
        <source>Could not clear old signatures on disk %1: %2 Restart the app and try again.</source>
        <translation>Alte Signaturen auf Datenträger %1 konnten nicht gelöscht werden: %2 Starten Sie die Anwendung neu und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>seek to the write position</source>
        <translation>die Schreibposition ansteuern</translation>
    </message>
    <message>
        <source>Could not clear old signatures on disk %1</source>
        <translation>Alte Signaturen auf Datenträger %1 konnten nicht gelöscht werden</translation>
    </message>
    <message>
        <source>Disk %1 stopped accepting writes while clearing old signatures. Unplug the drive, plug it back in and try again; if it keeps happening, try another USB port or replace the drive.</source>
        <translation>Datenträger %1 nimmt beim Löschen alter Signaturen keine Schreibzugriffe mehr an. Ziehen Sie das Laufwerk ab, stecken Sie es wieder ein und versuchen Sie es erneut; tritt der Fehler weiterhin auf, versuchen Sie einen anderen USB-Anschluss oder ersetzen Sie das Laufwerk.</translation>
    </message>
    <message>
        <source>Could not %1 on disk %2</source>
        <translation>Auf Datenträger %2 konnte folgende Aktion nicht ausgeführt werden: %1</translation>
    </message>
    <message>
        <source>Could not lock volume %1 before restore</source>
        <translation>Volume %1 konnte vor der Wiederherstellung nicht gesperrt werden</translation>
    </message>
    <message>
        <source>Could not dismount volume %1 before restore</source>
        <translation>Volume %1 konnte vor der Wiederherstellung nicht ausgehängt werden</translation>
    </message>
    <message>
        <source>Windows did not report USB disk %1.</source>
        <translation>Windows hat USB-Datenträger %1 nicht gemeldet.</translation>
    </message>
    <message>
        <source>Could not open volume %1 before restore</source>
        <translation>Volume %1 konnte vor der Wiederherstellung nicht geöffnet werden</translation>
    </message>
    <message>
        <source>Could not remove mount point %1</source>
        <translation>Einhängepunkt %1 konnte nicht entfernt werden</translation>
    </message>
    <message>
        <source>Windows did not report a new volume for disk %1 within %2 seconds.</source>
        <translation>Windows hat für Datenträger %1 innerhalb von %2 Sekunden kein neues Volume gemeldet.</translation>
    </message>
    <message>
        <source>No available drive letter was found.</source>
        <translation>Es wurde kein freier Laufwerksbuchstabe gefunden.</translation>
    </message>
    <message>
        <source>Could not mount the restored volume at %1</source>
        <translation>Das wiederhergestellte Volume konnte nicht unter %1 eingebunden werden</translation>
    </message>
    <message>
        <source>Could not reopen the restored volume</source>
        <translation>Das wiederhergestellte Volume konnte nicht erneut geöffnet werden</translation>
    </message>
    <message>
        <source>The restored volume no longer belongs to disk %1.</source>
        <translation>Das wiederhergestellte Volume gehört nicht mehr zu Datenträger %1.</translation>
    </message>
    <message>
        <source>Windows cannot format a volume as %1.</source>
        <translation>Windows kann ein Volume nicht als %1 formatieren.</translation>
    </message>
    <message>
        <source>No valid FAT32 allocation unit was selected before formatting.</source>
        <translation>Vor dem Formatieren wurde keine gültige FAT32-Zuordnungseinheit ausgewählt.</translation>
    </message>
    <message>
        <source>Windows did not report the restored volume for disk %1.</source>
        <translation>Windows hat das wiederhergestellte Volume für Datenträger %1 nicht gemeldet.</translation>
    </message>
    <message>
        <source>The format completed, but Windows did not report a %1 volume labelled %2 at %3.</source>
        <translation>Die Formatierung wurde abgeschlossen, aber Windows hat unter %3 kein %1-Volume mit der Bezeichnung %2 gemeldet.</translation>
    </message>
    <message>
        <source>Stopped before formatting disk %1: %2 If the original USB disk was unplugged, its partition table has already been rewritten. Plug it back in and run the restore on it again.</source>
        <translation>Vor dem Formatieren von Datenträger %1 angehalten: %2 Falls der ursprüngliche USB-Datenträger abgezogen wurde, wurde seine Partitionstabelle bereits neu geschrieben. Stecken Sie ihn wieder ein und starten Sie die Wiederherstellung darauf erneut.</translation>
    </message>
    <message>
        <source>Windows cannot format FAT32 on a volume larger than 32 GB. Choose exFAT or NTFS.</source>
        <translation>Windows kann Volumes über 32 GB nicht als FAT32 formatieren. Wählen Sie exFAT oder NTFS.</translation>
    </message>
    <message>
        <source>Windows cannot format FAT32 on a volume this small with %1-byte sectors. Choose exFAT or NTFS.</source>
        <translation>Windows kann ein so kleines Volume mit Sektoren von %1 Byte nicht als FAT32 formatieren. Wählen Sie exFAT oder NTFS.</translation>
    </message>
    <message>
        <source>ext4 is not available on Windows.</source>
        <translation>ext4 ist unter Windows nicht verfügbar.</translation>
    </message>
    <message>
        <source>verify the selected drive</source>
        <translation>das ausgewählte Laufwerk überprüfen</translation>
    </message>
    <message>
        <source>Opening and identifying the physical drive</source>
        <translation>Physisches Laufwerk öffnen und identifizieren</translation>
    </message>
    <message>
        <source>Refreshing Windows disk metadata</source>
        <translation>Windows-Datenträgermetadaten aktualisieren</translation>
    </message>
    <message>
        <source>refresh Windows disk metadata</source>
        <translation>die Windows-Datenträgermetadaten aktualisieren</translation>
    </message>
    <message>
        <source>Detaching existing volumes and drive letters</source>
        <translation>Vorhandene Volumes und Laufwerksbuchstaben trennen</translation>
    </message>
    <message>
        <source>detach the existing volumes and drive letters</source>
        <translation>die vorhandenen Volumes und Laufwerksbuchstaben trennen</translation>
    </message>
    <message>
        <source>Close any files, File Explorer windows and programs using the drive and run the restore again</source>
        <translation>Schließen Sie alle Dateien, Explorer-Fenster und Programme, die das Laufwerk verwenden, und starten Sie die Wiederherstellung erneut</translation>
    </message>
    <message>
        <source>Deleting existing partition records</source>
        <translation>Vorhandene Partitionseinträge löschen</translation>
    </message>
    <message>
        <source>delete the existing partitions</source>
        <translation>die vorhandenen Partitionen löschen</translation>
    </message>
    <message>
        <source>Locking the physical drive</source>
        <translation>Physisches Laufwerk sperren</translation>
    </message>
    <message>
        <source>Resetting the disk to RAW</source>
        <translation>Datenträger auf RAW zurücksetzen</translation>
    </message>
    <message>
        <source>Creating one %1 data partition</source>
        <translation>Eine %1-Datenpartition erstellen</translation>
    </message>
    <message>
        <source>Waiting for Windows to discover the new volume</source>
        <translation>Auf die Erkennung des neuen Volumes durch Windows warten</translation>
    </message>
    <message>
        <source>find the new volume</source>
        <translation>das neue Volume finden</translation>
    </message>
    <message>
        <source>verify the new volume</source>
        <translation>das neue Volume überprüfen</translation>
    </message>
    <message>
        <source>Assigning a drive letter</source>
        <translation>Laufwerksbuchstaben zuweisen</translation>
    </message>
    <message>
        <source>assign a drive letter</source>
        <translation>einen Laufwerksbuchstaben zuweisen</translation>
    </message>
    <message>
        <source>If every drive letter is in use, eject another drive to free one. Then unplug this drive, plug it back in and run the restore again</source>
        <translation>Falls alle Laufwerksbuchstaben belegt sind, werfen Sie ein anderes Laufwerk aus, um einen freizugeben. Ziehen Sie dann dieses Laufwerk ab, stecken Sie es wieder ein und starten Sie die Wiederherstellung erneut</translation>
    </message>
    <message>
        <source>format as %1</source>
        <translation>als %1 formatieren</translation>
    </message>
    <message>
        <source>another program is still using the drive</source>
        <translation>ein anderes Programm verwendet das Laufwerk noch</translation>
    </message>
    <message>
        <source>Close any files, File Explorer windows and programs using the drive and try again</source>
        <translation>Schließen Sie alle Dateien, Explorer-Fenster und Programme, die das Laufwerk verwenden, und versuchen Sie es erneut</translation>
    </message>
    <message>
        <source>the drive is write-protected</source>
        <translation>das Laufwerk ist schreibgeschützt</translation>
    </message>
    <message>
        <source>If it has a write-protect switch, turn it off and try again. If it has none, the drive may have made itself read-only and need replacing</source>
        <translation>Falls das Laufwerk einen Schreibschutzschalter hat, deaktivieren Sie ihn und versuchen Sie es erneut. Andernfalls hat es sich möglicherweise selbst schreibgeschützt und muss ersetzt werden</translation>
    </message>
    <message>
        <source>Windows refused access to the drive</source>
        <translation>Windows hat den Zugriff auf das Laufwerk verweigert</translation>
    </message>
    <message>
        <source>Close any programs using the drive. If it has a write-protect switch, turn it off and try again. If it stays read-only, the drive may have made itself read-only and need replacing</source>
        <translation>Schließen Sie alle Programme, die das Laufwerk verwenden. Falls es einen Schreibschutzschalter hat, deaktivieren Sie ihn und versuchen Sie es erneut. Bleibt es schreibgeschützt, hat es sich möglicherweise selbst schreibgeschützt und muss ersetzt werden</translation>
    </message>
    <message>
        <source>the drive could not complete the read or write</source>
        <translation>das Laufwerk konnte den Lese- oder Schreibvorgang nicht abschließen</translation>
    </message>
    <message>
        <source>Unplug it, plug it back in and try again; if it keeps happening, try another USB port or replace the drive</source>
        <translation>Ziehen Sie es ab, stecken Sie es wieder ein und versuchen Sie es erneut; tritt der Fehler weiterhin auf, versuchen Sie einen anderen USB-Anschluss oder ersetzen Sie das Laufwerk</translation>
    </message>
    <message>
        <source>the drive is not ready</source>
        <translation>das Laufwerk ist nicht bereit</translation>
    </message>
    <message>
        <source>the drive is no longer available</source>
        <translation>das Laufwerk ist nicht mehr verfügbar</translation>
    </message>
    <message>
        <source>Unplug it, plug it back in and try again; if it happens again the drive may be smaller than it claims</source>
        <translation>Ziehen Sie es ab, stecken Sie es wieder ein und versuchen Sie es erneut; tritt der Fehler erneut auf, ist das Laufwerk möglicherweise kleiner als angegeben</translation>
    </message>
    <message>
        <source>%1: %2 (Windows error %3). %4.</source>
        <translation>%1: %2 (Windows-Fehler %3). %4.</translation>
    </message>
    <message>
        <source>Close any programs using the drive, unplug it, plug it back in and run the restore again</source>
        <translation>Schließen Sie alle Programme, die das Laufwerk verwenden, ziehen Sie es ab, stecken Sie es wieder ein und starten Sie die Wiederherstellung erneut</translation>
    </message>
    <message>
        <source>

Details: %1</source>
        <translation>

Details: %1</translation>
    </message>
</context>
<context>
    <name>usbrestore::Core</name>
    <message>
        <source>RAW / unknown</source>
        <translation>RAW / unbekannt</translation>
    </message>
    <message>
        <source>Healthy</source>
        <translation>Fehlerfrei</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Warnung</translation>
    </message>
    <message>
        <source>Unhealthy</source>
        <translation>Fehlerhaft</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>Unbekannt</translation>
    </message>
    <message>
        <source>USB disk</source>
        <translation>USB-Laufwerk</translation>
    </message>
    <message>
        <source>en_US</source>
        <translation>de_DE</translation>
    </message>
    <message>
        <source>Refusing to zero %1 bytes at offset %2: not a multiple of the %3-byte sector size.</source>
        <translation>Das Löschen von %1 Bytes ab Position %2 wird abgelehnt: kein Vielfaches der Sektorgröße von %3 Bytes.</translation>
    </message>
    <message>
        <source>This disk reports no device path to restore.</source>
        <translation>Dieses Laufwerk meldet keinen Gerätepfad für die Wiederherstellung.</translation>
    </message>
    <message>
        <source>%1 is on the %2 bus, not USB.</source>
        <translation>%1 ist über den Bus %2 angeschlossen, nicht über USB.</translation>
    </message>
    <message>
        <source>%1 is a disk the running system depends on.</source>
        <translation>%1 wird vom laufenden System benötigt.</translation>
    </message>
    <message>
        <source>%1 is marked as a boot or system disk.</source>
        <translation>%1 ist als Boot- oder Systemlaufwerk markiert.</translation>
    </message>
    <message>
        <source>%1 holds a protected system location.</source>
        <translation>%1 enthält einen geschützten Systempfad.</translation>
    </message>
    <message>
        <source>%1 is offline.</source>
        <translation>%1 ist offline.</translation>
    </message>
    <message>
        <source>%1 is read-only.</source>
        <translation>%1 ist schreibgeschützt.</translation>
    </message>
    <message>
        <source>%1 reports no usable media.</source>
        <translation>%1 meldet kein nutzbares Speichermedium.</translation>
    </message>
    <message>
        <source>%1 reports only %2, which is too small to restore.</source>
        <translation>%1 meldet nur %2 und ist damit zu klein für die Wiederherstellung.</translation>
    </message>
    <message>
        <source>%1 reports an unsupported sector size of %2 bytes.</source>
        <translation>%1 meldet eine nicht unterstützte Sektorgröße von %2 Bytes.</translation>
    </message>
    <message>
        <source>%1 is too small for a safe partition layout.</source>
        <translation>%1 ist zu klein für ein sicheres Partitionsschema.</translation>
    </message>
    <message>
        <source>%1 no longer reports the same %2. Refresh and select the USB again.</source>
        <translation>Die Angabe „%2“ von %1 stimmt nicht mehr überein. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>The selected disk moved from %1 to %2. Refresh and select the USB again.</source>
        <translation>Das ausgewählte Laufwerk wurde von %1 nach %2 verschoben. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>size</source>
        <translation>Kapazität</translation>
    </message>
    <message>
        <source>device instance</source>
        <translation>Geräteinstanz</translation>
    </message>
    <message>
        <source>sector size</source>
        <translation>Sektorgröße</translation>
    </message>
    <message>
        <source>serial number</source>
        <translation>Seriennummer</translation>
    </message>
    <message>
        <source>unique ID</source>
        <translation>Eindeutige ID</translation>
    </message>
    <message>
        <source>device path</source>
        <translation>Gerätepfad</translation>
    </message>
    <message>
        <source>device name</source>
        <translation>Gerätename</translation>
    </message>
    <message>
        <source>Could not confirm that %1 is still the disk that was selected. Refresh and select the USB again.</source>
        <translation>Es konnte nicht bestätigt werden, dass %1 noch das ausgewählte Laufwerk ist. Aktualisieren Sie die Liste und wählen Sie das USB-Laufwerk erneut aus.</translation>
    </message>
    <message>
        <source>Unsupported sector size of %1 bytes.</source>
        <translation>Nicht unterstützte Sektorgröße von %1 Bytes.</translation>
    </message>
    <message>
        <source>The disk is too small for a partition layout.</source>
        <translation>Das Laufwerk ist zu klein für ein Partitionsschema.</translation>
    </message>
    <message>
        <source>No partition style was chosen.</source>
        <translation>Es wurde kein Partitionsschema ausgewählt.</translation>
    </message>
    <message>
        <source>MBR cannot address a partition this far into the disk. Choose GPT.</source>
        <translation>MBR kann eine Partition so weit hinten auf dem Laufwerk nicht adressieren. Wählen Sie GPT.</translation>
    </message>
    <message>
        <source>Large USB disk: %1. Make sure this is not an external SSD or HDD with data you need.</source>
        <translation>Großes USB-Laufwerk: %1. Stellen Sie sicher, dass dies keine externe SSD oder Festplatte mit benötigten Daten ist.</translation>
    </message>
    <message>
        <source>The partition layout is not aligned to the sector size.</source>
        <translation>Das Partitionsschema ist nicht an der Sektorgröße ausgerichtet.</translation>
    </message>
    <message>
        <source>The partition layout runs past the end of the disk.</source>
        <translation>Das Partitionsschema reicht über das Ende des Datenträgers hinaus.</translation>
    </message>
    <message>
        <source>The disk is too small to hold both GPT headers.</source>
        <translation>Der Datenträger ist für beide GPT-Kopfbereiche zu klein.</translation>
    </message>
    <message>
        <source>A GPT layout needs a disk and partition identifier.</source>
        <translation>Ein GPT-Schema benötigt eine Datenträger- und eine Partitionskennung.</translation>
    </message>
    <message>
        <source>The partition layout overlaps the GPT headers.</source>
        <translation>Das Partitionsschema überschneidet sich mit den GPT-Kopfbereichen.</translation>
    </message>
    <message>
        <source>An MBR layout needs a disk identifier.</source>
        <translation>Ein MBR-Schema benötigt eine Datenträgerkennung.</translation>
    </message>
    <message>
        <source>The restore failed.</source>
        <translation>Die Wiederherstellung ist fehlgeschlagen.</translation>
    </message>
    <message>
        <source>The restore failed.

Details: %1</source>
        <translation>Die Wiederherstellung ist fehlgeschlagen.

Details: %1</translation>
    </message>
    <message>
        <source>USB Restoration Tool and its restore helper do not agree on how to talk to each other, which usually means a partly upgraded installation. Reinstall the application.%1</source>
        <translation>USB Restoration Tool und sein Hilfsprogramm zur Wiederherstellung verwenden unterschiedliche Kommunikationsformate. Meist wurde die Installation nur teilweise aktualisiert. Installieren Sie die Anwendung neu.%1</translation>
    </message>
    <message>
        <source>

The helper said: %1</source>
        <translation>

Meldung des Hilfsprogramms: %1</translation>
    </message>
    <message>
        <source>Authentication was cancelled, so nothing on the drive was changed.</source>
        <translation>Die Authentifizierung wurde abgebrochen. Auf dem Laufwerk wurde nichts verändert.</translation>
    </message>
    <message>
        <source>The restore helper could not be started. Check that USB Restoration Tool is installed rather than run from a build directory.</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung konnte nicht gestartet werden. Prüfen Sie, ob USB Restoration Tool installiert ist und nicht aus einem Build-Verzeichnis gestartet wird.</translation>
    </message>
    <message>
        <source>The restore helper stopped unexpectedly (exit code %1).</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung wurde unerwartet beendet (Exit-Code %1).</translation>
    </message>
    <message>
        <source>The restore helper stopped unexpectedly (exit code %1): %2</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung wurde unerwartet beendet (Exit-Code %1): %2</translation>
    </message>
    <message>
        <source>The restore helper stopped unexpectedly. Check the drive with a partition tool before using it.</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung wurde unerwartet beendet. Prüfen Sie das Laufwerk vor der Verwendung mit einem Partitionierungsprogramm.</translation>
    </message>
    <message>
        <source>The restore helper finished without reporting where the volume ended up. Check the drive with a partition tool before using it.</source>
        <translation>Das Hilfsprogramm zur Wiederherstellung wurde beendet, ohne den Ort des neuen Volumes zu melden. Prüfen Sie das Laufwerk vor der Verwendung mit einem Partitionierungsprogramm.</translation>
    </message>
    <message>
        <source>an older version</source>
        <translation>einer älteren Version</translation>
    </message>
    <message>
        <source>a different build of version %1</source>
        <translation>einem anderen Build von Version %1</translation>
    </message>
    <message>
        <source>version %1</source>
        <translation>Version %1</translation>
    </message>
    <message>
        <source>USB Restoration Tool is version %1, but its restore helper is from %2, so the two disagree about how a restore proceeds. This usually means a partly upgraded installation. Reinstall the application so both come from the same release.</source>
        <translation>USB Restoration Tool hat Version %1, sein Hilfsprogramm zur Wiederherstellung stammt jedoch aus %2. Die Abläufe stimmen daher nicht überein. Meist wurde die Installation nur teilweise aktualisiert. Installieren Sie die Anwendung neu, damit beide aus derselben Veröffentlichung stammen.</translation>
    </message>
</context>
<context>
    <name>usbrestore::Disk</name>
    <message>
        <source>exFAT (recommended)</source>
        <translation>exFAT (empfohlen)</translation>
    </message>
    <message>
        <source>FAT32 (older devices)</source>
        <translation>FAT32 (ältere Geräte)</translation>
    </message>
    <message>
        <source>NTFS</source>
        <translation>NTFS</translation>
    </message>
    <message>
        <source>ext4 (Linux)</source>
        <translation>ext4 (Linux)</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>Unbekannt</translation>
    </message>
</context>
<context>
    <name>usbrestore::DiskService</name>
    <message>
        <source>USB Restoration Tool cannot get permission to restore USB disks.

Install it, so that it can ask for permission when a restore starts, or start it again with sudo:

    sudo %1</source>
        <translation>USB Restoration Tool kann keine Berechtigung erhalten, um USB-Laufwerke wiederherzustellen.

Installieren Sie das Programm, damit es beim Start einer Wiederherstellung nach Berechtigung fragen kann, oder starten Sie es erneut mit sudo:

    sudo %1</translation>
    </message>
    <message>
        <source>USB Restoration Tool needs Administrator permission to restore USB disks.

Close this window and start the app again, approving the Windows permission prompt.</source>
        <translation>USB Restoration Tool benötigt Administratorrechte, um USB-Laufwerke wiederherzustellen.

Schließen Sie dieses Fenster und starten Sie die Anwendung erneut. Bestätigen Sie dabei die Windows-Berechtigungsabfrage.</translation>
    </message>
</context>
<context>
    <name>usbrestore::Language</name>
    <message>
        <source>Language: Auto</source>
        <translation>Sprache: Automatisch</translation>
    </message>
    <message>
        <source>Language: English</source>
        <translation>Sprache: Englisch</translation>
    </message>
    <message>
        <source>Language: German</source>
        <translation>Sprache: Deutsch</translation>
    </message>
</context>
<context>
    <name>usbrestore::MainWindow</name>
    <message>
        <source>USB disk</source>
        <translation>USB-Laufwerk</translation>
    </message>
    <message>
        <source>not mounted</source>
        <translation>nicht eingehängt</translation>
    </message>
    <message>
        <source>Activity log</source>
        <translation>Aktivitätsprotokoll</translation>
    </message>
    <message>
        <source>A fresh start for your USB drive.</source>
        <translation>Ein Neuanfang für Ihr USB-Laufwerk.</translation>
    </message>
    <message>
        <source>Raw disk access needs elevated permission, and this process runs with it.</source>
        <translation>Der direkte Laufwerkszugriff erfordert erhöhte Rechte. Dieser Prozess verfügt darüber.</translation>
    </message>
    <message>
        <source>USER</source>
        <translation>BENUTZER</translation>
    </message>
    <message>
        <source>The app runs as an ordinary user. When a restore starts, the system asks for permission and a separate root helper does the writing.</source>
        <translation>Die Anwendung läuft mit normalen Benutzerrechten. Beim Start einer Wiederherstellung fragt das System nach Ihrer Zustimmung. Ein separater Hilfsprozess mit Root-Rechten übernimmt das Schreiben.</translation>
    </message>
    <message>
        <source>NO ACCESS</source>
        <translation>KEIN ZUGRIFF</translation>
    </message>
    <message>
        <source>Activity</source>
        <translation>Aktivität</translation>
    </message>
    <message>
        <source>Every step of this session, and a link to the log file.</source>
        <translation>Alle Schritte dieser Sitzung und ein Link zur Protokolldatei.</translation>
    </message>
    <message>
        <source>About</source>
        <translation>Über</translation>
    </message>
    <message>
        <source>01 / Select a drive</source>
        <translation>01 / Laufwerk auswählen</translation>
    </message>
    <message>
        <source>Detected USB disks</source>
        <translation>Erkannte USB-Laufwerke</translation>
    </message>
    <message>
        <source>&amp;Refresh</source>
        <translation>&amp;Aktualisieren</translation>
    </message>
    <message>
        <source>Drive details</source>
        <translation>Laufwerksdetails</translation>
    </message>
    <message>
        <source>No disk selected</source>
        <translation>Kein Laufwerk ausgewählt</translation>
    </message>
    <message>
        <source>Only disks reported on the USB bus are listed.</source>
        <translation>Es werden nur Laufwerke am USB-Bus angezeigt.</translation>
    </message>
    <message>
        <source>02 / Set up your restore</source>
        <translation>02 / Wiederherstellung einrichten</translation>
    </message>
    <message>
        <source>GPT (recommended)</source>
        <translation>GPT (empfohlen)</translation>
    </message>
    <message>
        <source>MBR (older devices)</source>
        <translation>MBR (ältere Geräte)</translation>
    </message>
    <message>
        <source>Partition style</source>
        <translation>Partitionsschema</translation>
    </message>
    <message>
        <source>GPT is what current systems expect and what Windows itself writes.
MBR is for devices that predate it and refuse to read a GPT stick:
BIOS-era PCs, car stereos, older TVs and set-top boxes.</source>
        <translation>GPT wird von aktuellen Systemen erwartet und auch von Windows selbst verwendet.
MBR ist für ältere Geräte gedacht, die GPT-Sticks nicht lesen können:
PCs aus der BIOS-Zeit, Autoradios, ältere Fernseher und Set-Top-Boxen.</translation>
    </message>
    <message>
        <source>Filesystem</source>
        <translation>Dateisystem</translation>
    </message>
    <message>
        <source>exFAT (recommended) works on Windows, Linux and macOS.
FAT32 is for older devices; files cannot be larger than 4 GB.
NTFS is Windows&apos; own filesystem; some cameras and consoles will not read it.
ext4 is Linux-only; Windows will not mount it without extra software.</source>
        <translation>exFAT (empfohlen) funktioniert unter Windows, Linux und macOS.
FAT32 ist für ältere Geräte gedacht; Dateien dürfen nicht größer als 4 GB sein.
NTFS ist das Windows-Dateisystem; manche Kameras und Konsolen können es nicht lesen.
ext4 ist für Linux; Windows kann es ohne Zusatzsoftware nicht einbinden.</translation>
    </message>
    <message>
        <source>Select a USB disk to restore.</source>
        <translation>Wählen Sie ein USB-Laufwerk zur Wiederherstellung aus.</translation>
    </message>
    <message>
        <source>Restore status</source>
        <translation>Wiederherstellungsstatus</translation>
    </message>
    <message>
        <source>Restore USB</source>
        <translation>USB wiederherstellen</translation>
    </message>
    <message>
        <source>Restore the selected USB disk</source>
        <translation>Ausgewähltes USB-Laufwerk wiederherstellen</translation>
    </message>
    <message>
        <source>Partition layout</source>
        <translation>Partitionsschema</translation>
    </message>
    <message>
        <source>Restoring erases every partition and file on the selected drive.</source>
        <translation>Die Wiederherstellung löscht alle Partitionen und Dateien auf dem ausgewählten Laufwerk.</translation>
    </message>
    <message>
        <source>Check the model and capacity before confirming. Boot, system, offline, read-only and non-USB disks are refused.</source>
        <translation>Prüfen Sie vor dem Bestätigen Modell und Kapazität. Boot-, System-, Offline- und schreibgeschützte Laufwerke sowie Laufwerke ohne USB-Anschluss werden abgelehnt.</translation>
    </message>
    <message>
        <source>Every step of this session. The same lines are written to the log file, which survives a restart.</source>
        <translation>Alle Schritte dieser Sitzung. Dieselben Zeilen werden in die Protokolldatei geschrieben, die auch nach einem Neustart erhalten bleibt.</translation>
    </message>
    <message>
        <source>Open log file</source>
        <translation>Protokolldatei öffnen</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>Schließen</translation>
    </message>
    <message>
        <source>Could not scan for USB disks.</source>
        <translation>Die Suche nach USB-Laufwerken ist fehlgeschlagen.</translation>
    </message>
    <message>
        <source>No USB disks detected. Plug the drive in and select Refresh.</source>
        <translation>Keine USB-Laufwerke erkannt. Schließen Sie das Laufwerk an und wählen Sie „Aktualisieren“.</translation>
    </message>
    <message>
        <source>1 USB disk detected.</source>
        <translation>1 USB-Laufwerk erkannt.</translation>
    </message>
    <message>
        <source>%1 USB disks detected.</source>
        <translation>%1 USB-Laufwerke erkannt.</translation>
    </message>
    <message>
        <source>Bus: %1    Current layout: %2    Sector size: %3 bytes
Filesystems: %4
Mounted at: %5
Volume labels: %6</source>
        <translation>Bus: %1    Aktuelles Schema: %2    Sektorgröße: %3 Bytes
Dateisysteme: %4
Eingehängt unter: %5
Volume-Namen: %6</translation>
    </message>
    <message>
        <source>Erases all contents. Creates one %1 partition formatted %2, labelled %3.</source>
        <translation>Löscht alle Inhalte. Erstellt eine %1-Partition mit dem Dateisystem %2 und dem Namen %3.</translation>
    </message>
    <message>
        <source>Nothing to restore until a USB disk is plugged in.</source>
        <translation>Schließen Sie ein USB-Laufwerk an, um es wiederherzustellen.</translation>
    </message>
    <message>
        <source>This disk will not be restored.</source>
        <translation>Dieses Laufwerk wird nicht wiederhergestellt.</translation>
    </message>
    <message>
        <source>This restore configuration cannot be used on this disk.</source>
        <translation>Diese Wiederherstellungseinstellungen können für dieses Laufwerk nicht verwendet werden.</translation>
    </message>
    <message>
        <source>Check this is the right disk.</source>
        <translation>Prüfen Sie, ob dies das richtige Laufwerk ist.</translation>
    </message>
    <message>
        <source>Ready to restore %1 as %2 %3.</source>
        <translation>Bereit, %1 als %2 mit %3 wiederherzustellen.</translation>
    </message>
    <message>
        <source>Contents: %1</source>
        <translation>Inhalt: %1</translation>
    </message>
    <message>
        <source>Restore refused</source>
        <translation>Wiederherstellung abgelehnt</translation>
    </message>
    <message>
        <source>Restoring %1...</source>
        <translation>%1 wird wiederhergestellt...</translation>
    </message>
    <message>
        <source>The device is responding again. Keep it connected until the restore finishes.</source>
        <translation>Das Gerät reagiert wieder. Lassen Sie es angeschlossen, bis die Wiederherstellung abgeschlossen ist.</translation>
    </message>
    <message>
        <source>No response from the device for %1. Keep it connected while the system waits.</source>
        <translation>Seit %1 keine Antwort vom Gerät. Lassen Sie es angeschlossen, während das System wartet.</translation>
    </message>
    <message>
        <source>The device has not responded for %1. Some sticks stall for minutes and then recover on their own. The restore has not changed the disk, but the system may still be flushing its own writes to it — unplug only if it never recovers, and expect to run the restore again afterwards.</source>
        <translation>Das Gerät antwortet seit %1 nicht. Manche USB-Sticks reagieren minutenlang nicht und erholen sich dann von selbst. Die Wiederherstellung hat das Laufwerk noch nicht verändert, aber das System schreibt möglicherweise noch eigene Daten darauf. Ziehen Sie es nur ab, wenn es sich nicht mehr erholt. Rechnen Sie damit, die Wiederherstellung danach erneut ausführen zu müssen.</translation>
    </message>
    <message>
        <source>The device has not responded for %1. Some sticks stall for minutes and then recover on their own. The disk is being rewritten — leave it plugged in and wait.</source>
        <translation>Das Gerät antwortet seit %1 nicht. Manche USB-Sticks reagieren minutenlang nicht und erholen sich dann von selbst. Das Laufwerk wird neu beschrieben. Lassen Sie es angeschlossen und warten Sie.</translation>
    </message>
    <message>
        <source>The restore stopped before erasing the disk.</source>
        <translation>Die Wiederherstellung wurde vor dem Löschen des Laufwerks angehalten.</translation>
    </message>
    <message>
        <source>The disk is now one %1 volume labelled %2 at %3.</source>
        <translation>Das Laufwerk enthält jetzt ein %1-Volume mit dem Namen %2 unter %3.</translation>
    </message>
    <message>
        <source>About USB Restoration Tool</source>
        <translation>Über USB Restoration Tool</translation>
    </message>
    <message>
        <source>USB Restoration Tool %1</source>
        <translation>USB Restoration Tool %1</translation>
    </message>
    <message>
        <source>A fresh start for your USB drive.

Created by %1

Restores a USB drive written by an ISO writer, Linux installer, or boot media tool to one GPT or MBR partition labelled USB. Choose exFAT, FAT32, or NTFS; Linux can also format ext4.

Confirmation, progress and the result stay in one window. Nothing is erased until you tick the acknowledgement and confirm.

Only USB disks are listed, and boot, system, offline, and read-only disks are refused.

Published builds are a Windows portable package, a Debian package, a Fedora RPM, and a Linux AppImage.</source>
        <translation>Ein Neuanfang für Ihr USB-Laufwerk.

Entwickelt von %1

Stellt ein USB-Laufwerk nach dem Beschreiben durch ein ISO-Programm, ein Linux-Installationsprogramm oder ein Tool für Startmedien wieder her. Erstellt eine GPT- oder MBR-Partition mit dem Namen USB. Wählen Sie exFAT, FAT32 oder NTFS; unter Linux ist auch ext4 verfügbar.

Bestätigung, Fortschritt und Ergebnis bleiben in einem Fenster. Es wird erst gelöscht, wenn Sie das Kontrollkästchen aktivieren und bestätigen.

Nur USB-Laufwerke werden angezeigt. Boot-, System-, Offline- und schreibgeschützte Laufwerke werden abgelehnt.

Verfügbar als portables Windows-Paket, Debian-Paket, Fedora-RPM und Linux-AppImage.</translation>
    </message>
    <message>
        <source>Open GitHub</source>
        <translation>GitHub öffnen</translation>
    </message>
    <message>
        <source>No log file</source>
        <translation>Keine Protokolldatei</translation>
    </message>
    <message>
        <source>No log file has been written yet.</source>
        <translation>Es wurde noch keine Protokolldatei geschrieben.</translation>
    </message>
    <message>
        <source>Restore in progress</source>
        <translation>Wiederherstellung läuft</translation>
    </message>
    <message>
        <source>A restore is writing to the disk. Wait for it to finish before closing; stopping now would leave the disk unusable.</source>
        <translation>Die Wiederherstellung schreibt auf das Laufwerk. Warten Sie vor dem Schließen, bis sie abgeschlossen ist. Ein Abbruch würde das Laufwerk unbrauchbar hinterlassen.</translation>
    </message>
    <message>
        <source>A restore is still running. Wait for it to finish before closing. The disk has not been changed yet, but closing now would stop the restore.</source>
        <translation>Eine Wiederherstellung läuft noch. Warten Sie vor dem Schließen, bis sie abgeschlossen ist. Das Laufwerk wurde noch nicht verändert, aber das Schließen würde die Wiederherstellung abbrechen.</translation>
    </message>
</context>
<context>
    <name>usbrestore::RestoreDialog</name>
    <message>
        <source>Check drive safety</source>
        <translation>Laufwerkssicherheit prüfen</translation>
    </message>
    <message>
        <source>Confirm drive identity</source>
        <translation>Laufwerksidentität bestätigen</translation>
    </message>
    <message>
        <source>Unmount existing volumes</source>
        <translation>Vorhandene Volumes aushängen</translation>
    </message>
    <message>
        <source>Claim exclusive access</source>
        <translation>Exklusiven Zugriff sichern</translation>
    </message>
    <message>
        <source>Open the drive</source>
        <translation>Laufwerk öffnen</translation>
    </message>
    <message>
        <source>Refresh drive information</source>
        <translation>Laufwerksinformationen aktualisieren</translation>
    </message>
    <message>
        <source>Disconnect existing volumes</source>
        <translation>Vorhandene Volumes trennen</translation>
    </message>
    <message>
        <source>Remove old partitions</source>
        <translation>Alte Partitionen entfernen</translation>
    </message>
    <message>
        <source>Lock the drive</source>
        <translation>Laufwerk sperren</translation>
    </message>
    <message>
        <source>Clear old disk signatures</source>
        <translation>Alte Datenträgersignaturen löschen</translation>
    </message>
    <message>
        <source>Reset the disk layout</source>
        <translation>Datenträgeraufteilung zurücksetzen</translation>
    </message>
    <message>
        <source>Write the partition table</source>
        <translation>Partitionstabelle schreiben</translation>
    </message>
    <message>
        <source>Create the new partition</source>
        <translation>Neue Partition erstellen</translation>
    </message>
    <message>
        <source>Reload the partition table</source>
        <translation>Partitionstabelle neu einlesen</translation>
    </message>
    <message>
        <source>Wait for the new partition</source>
        <translation>Auf die neue Partition warten</translation>
    </message>
    <message>
        <source>Wait for the new volume</source>
        <translation>Auf das neue Volume warten</translation>
    </message>
    <message>
        <source>Refresh the new volume</source>
        <translation>Neues Volume aktualisieren</translation>
    </message>
    <message>
        <source>Assign a drive letter</source>
        <translation>Laufwerksbuchstaben zuweisen</translation>
    </message>
    <message>
        <source>Format the filesystem</source>
        <translation>Dateisystem formatieren</translation>
    </message>
    <message>
        <source>Finish the restore</source>
        <translation>Wiederherstellung abschließen</translation>
    </message>
    <message>
        <source>Restore step</source>
        <translation>Wiederherstellungsschritt</translation>
    </message>
    <message>
        <source>Confirm USB restore</source>
        <translation>USB-Wiederherstellung bestätigen</translation>
    </message>
    <message>
        <source>CHECK BEFORE ERASING</source>
        <translation>VOR DEM LÖSCHEN PRÜFEN</translation>
    </message>
    <message>
        <source>Erase this USB drive?</source>
        <translation>Dieses USB-Laufwerk löschen?</translation>
    </message>
    <message>
        <source>%1  /  %2  /  Label: %3</source>
        <translation>%1  /  %2  /  Bezeichnung: %3</translation>
    </message>
    <message>
        <source>This cannot be undone.</source>
        <translation>Dies kann nicht rückgängig gemacht werden.</translation>
    </message>
    <message>
        <source>Every partition and file on this drive will be permanently erased.</source>
        <translation>Alle Partitionen und Dateien auf diesem Laufwerk werden unwiderruflich gelöscht.</translation>
    </message>
    <message>
        <source>Creates one %1 partition formatted %2 and labelled %3.</source>
        <translation>Erstellt eine %1-Partition mit dem Dateisystem %2 und der Bezeichnung %3.</translation>
    </message>
    <message>
        <source>Mounted at: %1
Volume labels: %2</source>
        <translation>Eingehängt unter: %1
Volume-Bezeichnungen: %2</translation>
    </message>
    <message>
        <source>I understand that everything on %1
will be permanently erased.</source>
        <translation>Ich verstehe, dass alle Daten auf %1
unwiderruflich gelöscht werden.</translation>
    </message>
    <message>
        <source>Preparing the restore. Approve the system permission prompt if asked.</source>
        <translation>Wiederherstellung wird vorbereitet. Bestätigen Sie bei Aufforderung die Berechtigungsabfrage des Systems.</translation>
    </message>
    <message>
        <source>Preparing · %1 steps</source>
        <translation>Vorbereitung · %1 Schritte</translation>
    </message>
    <message>
        <source>Elapsed 0:00</source>
        <translation>Vergangen 0:00</translation>
    </message>
    <message>
        <source>Completed restore steps, not a time estimate</source>
        <translation>Abgeschlossene Wiederherstellungsschritte, keine Zeitschätzung</translation>
    </message>
    <message>
        <source>Waiting</source>
        <translation>Wartet</translation>
    </message>
    <message>
        <source>Keep the drive connected. You can request cancellation before writing begins.</source>
        <translation>Lassen Sie das Laufwerk angeschlossen. Sie können abbrechen, bevor der Schreibvorgang beginnt.</translation>
    </message>
    <message>
        <source>Restore safety information</source>
        <translation>Sicherheitshinweise zur Wiederherstellung</translation>
    </message>
    <message>
        <source>Open log file</source>
        <translation>Protokolldatei öffnen</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <source>Erase and restore</source>
        <translation>Löschen und wiederherstellen</translation>
    </message>
    <message>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <source>Cancel restore</source>
        <translation>Wiederherstellung abbrechen</translation>
    </message>
    <message>
        <source>Restoring USB drive</source>
        <translation>USB-Laufwerk wird wiederhergestellt</translation>
    </message>
    <message>
        <source>A FRESH START</source>
        <translation>EIN NEUANFANG</translation>
    </message>
    <message>
        <source>Restoring your USB</source>
        <translation>USB wird wiederhergestellt</translation>
    </message>
    <message>
        <source>Elapsed %1:%2</source>
        <translation>Vergangen %1:%2</translation>
    </message>
    <message>
        <source>Done · %1 s</source>
        <translation>Fertig · %1 s</translation>
    </message>
    <message>
        <source>In progress</source>
        <translation>Läuft</translation>
    </message>
    <message>
        <source>Step %1 of %2</source>
        <translation>Schritt %1 von %2</translation>
    </message>
    <message>
        <source>Writing in progress</source>
        <translation>Schreibvorgang läuft</translation>
    </message>
    <message>
        <source>The disk is being rewritten. Keep it connected and wait for the result.</source>
        <translation>Der Datenträger wird neu beschrieben. Lassen Sie ihn angeschlossen und warten Sie auf das Ergebnis.</translation>
    </message>
    <message>
        <source>Keep the drive connected. Cancellation stops at the next safe checkpoint.</source>
        <translation>Lassen Sie das Laufwerk angeschlossen. Der Abbruch erfolgt am nächsten sicheren Haltepunkt.</translation>
    </message>
    <message>
        <source>Stopping…</source>
        <translation>Wird angehalten…</translation>
    </message>
    <message>
        <source>Cancellation requested. Waiting for a safe checkpoint; if writing has already begun, the restore will finish instead. Keep the drive connected.</source>
        <translation>Abbruch angefordert. Warten auf einen sicheren Haltepunkt. Falls der Schreibvorgang bereits begonnen hat, wird die Wiederherstellung stattdessen abgeschlossen. Lassen Sie das Laufwerk angeschlossen.</translation>
    </message>
    <message>
        <source>All %1 steps completed</source>
        <translation>Alle %1 Schritte abgeschlossen</translation>
    </message>
    <message>
        <source>Your USB drive is ready to use. Eject it through your system before unplugging.</source>
        <translation>Ihr USB-Laufwerk ist einsatzbereit. Werfen Sie es über Ihr System aus, bevor Sie es abziehen.</translation>
    </message>
    <message>
        <source>Failed</source>
        <translation>Fehlgeschlagen</translation>
    </message>
    <message>
        <source>Stopped</source>
        <translation>Angehalten</translation>
    </message>
    <message>
        <source>Restore not completed</source>
        <translation>Wiederherstellung nicht abgeschlossen</translation>
    </message>
    <message>
        <source>Cancelled before erasing</source>
        <translation>Vor dem Löschen abgebrochen</translation>
    </message>
    <message>
        <source>Read the error above before retrying. The log file contains more details.</source>
        <translation>Lesen Sie vor einem erneuten Versuch die Fehlermeldung oben. Weitere Details stehen in der Protokolldatei.</translation>
    </message>
    <message>
        <source>Existing volumes may need to be mounted again or have their drive letters reassigned.</source>
        <translation>Vorhandene Volumes müssen möglicherweise erneut eingehängt oder ihre Laufwerksbuchstaben neu zugewiesen werden.</translation>
    </message>
    <message>
        <source>Not run</source>
        <translation>Nicht ausgeführt</translation>
    </message>
    <message>
        <source>Restore complete</source>
        <translation>Wiederherstellung abgeschlossen</translation>
    </message>
    <message>
        <source>Restore failed</source>
        <translation>Wiederherstellung fehlgeschlagen</translation>
    </message>
    <message>
        <source>Restore cancelled</source>
        <translation>Wiederherstellung abgebrochen</translation>
    </message>
</context>
<context>
    <name>usbrestore::Startup</name>
    <message>
        <source>Elevated permission required</source>
        <translation>Erhöhte Berechtigung erforderlich</translation>
    </message>
</context>
<context>
    <name>usbrestore::Theme</name>
    <message>
        <source>Theme: Auto</source>
        <translation>Design: Automatisch</translation>
    </message>
    <message>
        <source>Theme: Light</source>
        <translation>Design: Hell</translation>
    </message>
    <message>
        <source>Theme: Dark</source>
        <translation>Design: Dunkel</translation>
    </message>
    <message>
        <source>Theme</source>
        <translation>Design</translation>
    </message>
</context>
</TS>
