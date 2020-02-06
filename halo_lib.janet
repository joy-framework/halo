(defn server
  "Creates a simple http server"
  [handler port &opt ip-address]
  (print (string/format "Server listening on [%s:%d] ..." (or ip-address "localhost") port))
  (start-server handler port ip-address))
