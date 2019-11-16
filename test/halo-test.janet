(import build/halo :as halo)

(defn handler [request]
  (print request)
  {:status 200 :body "ITS WORKING. SUCH A CRAPPY SERVER BUT STILL" :headers {"Content-Type" "text/plain"}})

(halo/server handler 8080)
