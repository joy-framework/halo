## halo 😇

Halo will be an http server for [janet](https://github.com/janet-lang/janet)

**This project is in early development so expect major changes**

```clojure
(import halo)

(defn handler [request]
  {:status 200 :body "halo 😇" :headers {"Content-Type" "text/plain"}))

(halo/server handler 8080)
```
