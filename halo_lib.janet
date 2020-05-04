(defn server
  "Creates a simple http server"
  [handler port &opt ip-address]
  (def port (string port))
  (print (string/format "Server listening on [%s:%s] ..." (or ip-address "localhost") port))
  (start-server handler port ip-address)

  (while (server-running?)
    (poll-server 1000))

  (stop-server))
