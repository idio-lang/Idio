;; define Scheme's primitives from Idio's
(define <= le)
(define < lt)
(define = ==)
(define >= ge)
(define > gt)

(define port? handle?)
(define input-port? input-handle?)
(define output-port? output-handle?)
(define current-input-port current-input-handle)
(define current-output-port current-output-handle)
(define set-input-port! set-input-handle!)
(define set-output-port! set-output-handle!)
(define close-port close-handle)
(define close-input-port close-input-handle)
(define close-output-port close-output-handle)
(define port-closed? handle-closed?)

(define cons pair)
(define car ph)
(define set-car! set-ph!)
(define cdr pt)
(define set-cdr! set-pt!)

(define read scm-read)

