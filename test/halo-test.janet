(import build/halo :as halo)

(defn handler [request]
  (print (string/format "%q" request))
  {:status 200 :body "It's working! It's working!" :headers {"Content-Type" "text/plain"}})

(halo/server handler 8080)
