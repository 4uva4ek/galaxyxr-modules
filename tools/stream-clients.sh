# Streaming client main processes, sourced by app-only modules.
# A package matches when it starts with the first three labels of Steam Link
# (com.valvesoftware.steamlinkvr) or ChronoLink (dev.chronolink.client); the
# rest (.x_23, .test, ...) is ignored. "package:name" secondary processes are
# not targets. Prints space-separated PIDs, like pidof; nothing when none run.
stream_client_pids() {
  ps -A -o PID=,NAME= | awk '$2 ~ /^(com\.valvesoftware\.steamlinkvr|dev\.chronolink\.client)/ && $2 !~ /:/ { printf "%s%s", sep, $1; sep = " " } END { if (sep != "") print "" }'
}
