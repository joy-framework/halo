(defn server
  "Creates a simple http server"
  [handler port &opt ip-address]
  (print (string/format "Server listening on [%s:%d] ..." (or ip-address "localhost") port))
  (start-server handler (string port) ip-address)

  (while (server-running?)
    (poll-server 1000))

  (stop-server))
