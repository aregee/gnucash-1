;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; balance-sheet.scm: balance sheet 
;; 
;; By Robert Merkel <rgmerk@mira.net>
;;
;; Largely borrowed from pnl.scm by:
;; Christian Stimming <stimming@tu-harburg.de>
;;
;; This program is free software; you can redistribute it and/or    
;; modify it under the terms of the GNU General Public License as   
;; published by the Free Software Foundation; either version 2 of   
;; the License, or (at your option) any later version.              
;;                                                                  
;; This program is distributed in the hope that it will be useful,  
;; but WITHOUT ANY WARRANTY; without even the implied warranty of   
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    
;; GNU General Public License for more details.                     
;;                                                                  
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, contact:
;;
;; Free Software Foundation           Voice:  +1-617-542-5942
;; 59 Temple Place - Suite 330        Fax:    +1-617-542-2652
;; Boston, MA  02111-1307,  USA       gnu@gnu.org
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(gnc:support "report/balance-sheet.scm")
(gnc:depend  "report-html.scm")

;; first define all option's names so that they are properly defined
;; in *one* place.

(let* ((pagename-general (N_ "General"))
       (optname-from-date (N_ "From"))
       (optname-to-date (N_ "To"))
       
       (pagename-accounts (N_ "Accounts"))
       (optname-display-depth (N_ "Account Display Depth"))
       (optname-show-subaccounts (N_ "Always show sub-accounts"))
       (optname-accounts (N_ "Account"))
       (optname-show-parent-balance (N_ "Show balances for parent accounts"))
       (optname-show-parent-total (N_ "Show subtotals"))
       
;;      (pagename-currencies (N_ "Currencies")) too little options :)
       (pagename-currencies pagename-general)
       (optname-show-foreign (N_ "Show Foreign Currencies"))
       (optname-report-currency (N_ "Report's currency")))

  ;; Moderatly ugly hack here, i.e. this depends on the internal
  ;; structure of html-table -- if that is changed, this might break.
  (define (html-table-merge t1 t2)
    (begin 
      (gnc:html-table-set-data! t1
			      (append
			       (gnc:html-table-data t2)
			       (gnc:html-table-data t1)))
      (gnc:html-table-set-num-rows-internal!
       t1 (+ (gnc:html-table-num-rows t1)
	     (gnc:html-table-num-rows t2)))))

  ;; Copied from html-utilities.scm.
  ;; Creates the table cell with given colspan (and rowspan=1), with
  ;; the content content and in boldface if boldface? is true. content
  ;; may be #f (empty cell), or a string, or a html-text
  ;; object. Returns a html-table-cell object.
  (define (my-table-cell colspan content boldface?)
    (gnc:make-html-table-cell/size 
     1 colspan 
     (and content ;; if content == #f, just use #f
	  (if boldface? 
	      ;; Further improvement: use some other table cell
	      ;; style here ("grand-total") instead of the direct
	      ;; markup-b.
	      (gnc:make-html-text
	       (if (gnc:html-text? content)
		   (apply gnc:html-markup-b 
			  (gnc:html-text-body content))
		   (gnc:html-markup-b content)))
	      content))))
  
  (define (accountlist-get-comm-balance-at-date accountlist date)
    (let ((collector (gnc:make-commodity-collector)))
      (for-each (lambda (account)
		  (let ((balance 
			(gnc:account-get-comm-balance-at-date 
			 account date #f)))
		    (collector 'merge balance #f)))
		accountlist)
      collector))
		  
  ;; options generator
  (define (balance-sheet-options-generator)
    (let ((options (gnc:new-options)))
      
      ;; date at which to report balance
      (gnc:options-add-report-date!
       options pagename-general 
       optname-to-date "a")

      ;; all about currencies
      (gnc:options-add-currency-selection!
       options pagename-currencies
       optname-show-foreign optname-report-currency
       "b")

      ;; accounts to work on
      (gnc:options-add-account-selection! 
       options pagename-accounts
       optname-display-depth optname-show-subaccounts
       optname-accounts "a" 2
       (lambda ()
	 (gnc:filter-accountlist-type 
	  '(bank cash credit asset liability stock mutual-fund currency
            equity income expense)
	  (gnc:group-get-subaccounts (gnc:get-current-group)))))
      
      ;; what to show about non-leaf accounts
      (gnc:register-option 
       options
       (gnc:make-simple-boolean-option
	pagename-accounts optname-show-parent-balance 
	"c" (N_ "Show balances for parent accounts") #f))

      ;; have a subtotal for each parent account?
      (gnc:register-option 
       options
       (gnc:make-simple-boolean-option
	pagename-accounts optname-show-parent-total
	"d" (N_ "Show subtotals for parent accounts") #f))

      ;; Set the general page as default option tab
      (gnc:options-set-default-section options pagename-general)      

      options))
  
  ;;;;;;;;;;;;;;;;;;;;;;;;;;;
  ;; balance-sheet-renderer
  ;; set up the document and add the table
  ;;;;;;;;;;;;;;;;;;;;;;;;;;;

  (define (balance-sheet-renderer report-obj)
    (define (get-option pagename optname)
      (gnc:option-value
       (gnc:lookup-option 
        (gnc:report-options report-obj) pagename optname)))

    ;; get all option's values
    (let* ((display-depth (get-option pagename-accounts 
				      optname-display-depth))
	   (show-subaccts? (get-option pagename-accounts
				      optname-show-subaccounts))
	   (accounts (get-option pagename-accounts
				 optname-accounts))	 
	   (show-parent-balance? (get-option pagename-accounts
					     optname-show-parent-balance))
	   (show-parent-total? (get-option pagename-accounts
					   optname-show-parent-total))
	   (show-fcur? (get-option pagename-currencies
				   optname-show-foreign))
	   (report-currency (get-option pagename-currencies
					optname-report-currency))
	   (to-date-tp (gnc:timepair-end-day-time 
		       (vector-ref (get-option pagename-general
					       optname-to-date) 1)))

	   ;; decompose the account list
	   (split-up-accounts (gnc:decompose-accountlist accounts))
	   (dummy (gnc:warn "split-up-accounts" split-up-accounts))
	   (asset-accounts
	    (assoc-ref split-up-accounts (_ "Assets")))
	   (liability-accounts
	    (assoc-ref split-up-accounts (_ "Liabilities")))
;	   (liability-account-names
;	    (map gnc:account-get-name liability-accounts))
;	   (dummy2 
;	    (gnc:warn "liability-account-names" liability-account-names))
	   (equity-accounts
	    (assoc-ref split-up-accounts (_"Equity")))
	   (income-expense-accounts
	    (append (assoc-ref split-up-accounts (_ "Income"))
		    (assoc-ref split-up-accounts (_ "Expense"))))

	   ;; cstim: happy now? :->

	   (doc (gnc:make-html-document))
	   (txt (gnc:make-html-text))
	   (tree-depth (if (equal? display-depth 'all)
			   (gnc:get-current-group-depth) 
			   display-depth))
	   ;; calculate the exchange rates  
	   (exchange-alist (gnc:make-exchange-alist 
			    report-currency to-date-tp))
	   (exchange-fn (gnc:make-exchange-function exchange-alist))
	   (totals-get-balance (lambda (account)
				 (gnc:account-get-comm-balance-at-date 
				  account to-date-tp #f))))

      (define (add-subtotal-line table label balance)
	(if show-fcur?
	    ;; FIXME: The multi-currency format is not yet adapted to
	    ;; take tree-depth into account. Instead of coding that
	    ;; here it would definitely be better to extract the
	    ;; necessary function out of html-build-acct-table into
	    ;; the global namespace.
	    (let ((first-row #t))
	      (balance 'format
		       (lambda (commodity amount)
			 (html-table-append-row!
			  (list (if first-row
				    (begin 
				      (set! first-row #f)
				      label)
				    #f)
				(gnc:make-gnc-monetary
				 commodity amount))))
		       #f))
	    (gnc:html-table-append-row!
	     table (append 
		    ;; FIXME: is it possible to get rid of my private
		    ;; definition of my-table-cell? Maybe as another
		    ;; extracted funtion from html-build-acct-tree.
		    (list (my-table-cell tree-depth label #t))
		    (gnc:html-make-empty-cells (- tree-depth 1))
		    (list (and balance
			       (gnc:make-html-text
				;; FIXME: this markup-b can go away as
				;; soon as we have styles here.
				(gnc:html-markup-b
				 (gnc:sum-collector-commodity 
				  balance report-currency exchange-fn)))))))))

      ;;(gnc:warn "account names" liability-account-names)
      (gnc:html-document-set-title! 
       ;; FIXME: Use magic sprintf code.
       doc (sprintf #f (_ "Balance sheet at %s")
		    (gnc:timepair-to-datestring to-date-tp)))

      (if (not (null? accounts))
	  ;; Get all the balances for each account group.
	  (let* ((asset-balance 
		  (gnc:accounts-get-comm-total-assets 
		   asset-accounts totals-get-balance))
		 (liability-balance
		  (gnc:accounts-get-comm-total-assets 
		   liability-accounts totals-get-balance))
		 (equity-balance
		  (gnc:accounts-get-comm-total-assets 
		   equity-accounts totals-get-balance))
		 (sign-reversed-liability-balance
		  (gnc:make-commodity-collector))
		 (neg-retained-profit-balance 
		  (accountlist-get-comm-balance-at-date
		   income-expense-accounts
		   to-date-tp))
		 (retained-profit-balance (gnc:make-commodity-collector))
		 (total-equity-balance (gnc:make-commodity-collector))
		 (equity-plus-liability (gnc:make-commodity-collector))

		 ;; Create the account tables here.
		 (asset-table 
		  (gnc:html-build-acct-table 
		   #f to-date-tp 
		   tree-depth show-subaccts? 
		   asset-accounts
		   #f #f #f #f
		   ;;gnc:accounts-get-comm-total-assets (_ "Assets") 
		   #f
		   show-parent-balance? show-parent-total?
		   show-fcur? report-currency exchange-fn))
		 (liability-table 
		  (gnc:html-build-acct-table
		   #f to-date-tp
		   tree-depth show-subaccts?
		   liability-accounts
		   #f #f #f #f
		   ;;gnc:accounts-get-comm-total-assets (_ "Liabilities") 
		   #f
		   show-parent-balance? show-parent-total?
		   show-fcur? report-currency exchange-fn))
		 (equity-table
		  (gnc:html-build-acct-table
		   #f to-date-tp
		   tree-depth show-subaccts?
		   equity-accounts
		   #f #f #f #f 
		   ;;gnc:accounts-get-comm-total-assets (_ "Equity") 
		   #f 
		   show-parent-balance? show-parent-total?
		   show-fcur? report-currency exchange-fn)))

	    (retained-profit-balance 'minusmerge
				     neg-retained-profit-balance
				     #f)
	    (total-equity-balance 'minusmerge equity-balance #f)
	    (total-equity-balance 'merge
				  retained-profit-balance
				  #f)	    
	    (sign-reversed-liability-balance 'minusmerge
					     liability-balance
					     #f)
	    (equity-plus-liability 'merge
				   sign-reversed-liability-balance
				   #f)
	    (equity-plus-liability 'merge
				   total-equity-balance
				   #f)

	    
	    ;; Now concatenate the tables. This first prepend-row has
	    ;; to be written out by hand -- we can't use the function
	    ;; append-something because we have to prepend.
	    (gnc:html-table-prepend-row! 
	     asset-table 
	     (list (my-table-cell (* (if show-fcur? 3 2) 
				     tree-depth) 
				  (_ "Assets") #t)))
	    
	    (add-subtotal-line 
	     asset-table (_ "Assets") asset-balance)	    
	    
	    ;; add a horizontal ruler
	    (gnc:html-table-append-ruler! 
	     asset-table (* (if show-fcur? 3 2) tree-depth))
	    
	    (add-subtotal-line 
	     asset-table (_ "Liabilities") #f)
	    (html-table-merge asset-table liability-table)
	    (add-subtotal-line
	     asset-table (_ "Liabilities") sign-reversed-liability-balance)

	    (gnc:html-table-append-ruler! 
	     asset-table (* (if show-fcur? 3 2) tree-depth))
	    (add-subtotal-line
	     asset-table (_ "Equity") #f)
	    (html-table-merge asset-table equity-table)
	    (add-subtotal-line
             asset-table (_ "Net Profit") retained-profit-balance)
	    (add-subtotal-line
             asset-table (_ "Total Equity") total-equity-balance)

	    (gnc:html-table-append-ruler! 
	     asset-table (* (if show-fcur? 3 2) tree-depth))
	    (add-subtotal-line
	     asset-table (_ "Liabilities & Equity") equity-plus-liability)
	    (gnc:html-document-add-object! doc asset-table)

	    ;; add currency information
;	    (gnc:html-document-add-object! 
;	     doc ;;(gnc:html-markup-p
;	     (gnc:html-make-exchangerates 
;	      report-currency exchange-alist accounts #f)))
	    )
	  
	  ;; error condition: no accounts specified
          (let ((p (gnc:make-html-text)))
            (gnc:html-text-append! 
             p 
             (gnc:html-markup-h2 (_ "No accounts selected"))
             (gnc:html-markup-p
              (_ "This report requires accounts to be selected.")))
            (gnc:html-document-add-object! doc p)))      
      doc))

  (gnc:define-report 
   'version 1
   'name (N_ "Balance Sheet")
   'options-generator balance-sheet-options-generator
   'renderer balance-sheet-renderer))
