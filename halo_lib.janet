(defn server
  "Creates a simple http server"
  [handler port &opt ip-address]
  (default ip-address "127.0.0.1")
  (print (string/format "Server listening on [%s:%d] ..." ip-address port))
  (start-server handler port ip-address))
